#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include <QApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace {
QString opContainerStyle(bool isDark, bool selected, bool hovered) {
    if (selected)
        return isDark
            ? u"#opContainer { background: #21466f; border-bottom: 2px solid #60cdff; }"_s
            : u"#opContainer { background: #dbeafe; border-bottom: 2px solid #005fb7; }"_s;
    if (hovered)
        return isDark ? u"#opContainer { background: #393939; }"_s : u"#opContainer { background: #f5f5f5; }"_s;
    return QString();
}

void updateOpWidgetStyle(DesktopSelectTable* table, int row) {
    if (!table || row < 0 || row >= table->rowCount())
        return;
    QWidget* widget = table->cellWidget(row, table->columnCount() - 1);
    if (!widget)
        return;
    bool selected = table->selectionModel() && table->selectionModel()->isRowSelected(row, QModelIndex());
    bool hovered = row == table->hoveredRow();
    widget->setStyleSheet(opContainerStyle(currentThemeName() == "dark", selected, hovered));
}
}

int DesktopSelectTable::dropTargetRow(const QPoint& pos) {
    int rc = rowCount();
    if (rc == 0)
        return 0;
    for (int row = 0; row < rc; ++row) {
        QRect rect = visualRect(model()->index(row, 0));
        rect.setLeft(0);
        rect.setRight(viewport()->width());
        if (pos.y() < rect.center().y())
            return row;
    }
    return rc;
}

void DesktopSelectTable::updateHoverRow(const QPoint& pos) {
    QModelIndex idx = indexAt(pos);
    int newRow = idx.isValid() ? idx.row() : -1;
    if (newRow == m_hoverRow)
        return;

    int oldRow = m_hoverRow;
    m_hoverRow = newRow;
    updateOpWidgetStyle(this, oldRow);
    updateOpWidgetStyle(this, newRow);
    viewport()->update();
}

void DesktopSelectTable::restartToolTipTimer(const QModelIndex& index, const QPoint& pos) {
    if (index == m_toolTipIndex) {
        m_toolTipPos = pos;
        if (QToolTip::isVisible() || (m_toolTipTimer && m_toolTipTimer->isActive()))
            return;
    }

    m_toolTipIndex = index;
    m_toolTipPos = pos;

    if (!m_toolTipTimer) {
        m_toolTipTimer = new QTimer(this);
        m_toolTipTimer->setSingleShot(true);
        connect(m_toolTipTimer, &QTimer::timeout, this, [this]() {
            if (!m_toolTipIndex.isValid())
                return;

            QString text = model()->data(m_toolTipIndex, Qt::ToolTipRole).toString();
            if (text.isEmpty())
                text = model()->data(m_toolTipIndex, Qt::DisplayRole).toString();
            if (text.isEmpty())
                return;

            QToolTip::showText(viewport()->mapToGlobal(m_toolTipPos), text, viewport());
        });
    }

    QToolTip::hideText();
    if (m_toolTipIndex.isValid())
        m_toolTipTimer->start(1000);
    else
        m_toolTipTimer->stop();
}

void DesktopSelectTable::hideDelayedToolTip() {
    m_toolTipIndex = QPersistentModelIndex();
    if (m_toolTipTimer)
        m_toolTipTimer->stop();
    QToolTip::hideText();
}

bool DesktopSelectTable::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* helpEvent = static_cast<QHelpEvent*>(event);
        restartToolTipTimer(indexAt(helpEvent->pos()), helpEvent->pos());
        return true;
    }

    if (event->type() == QEvent::Leave)
        hideDelayedToolTip();

    return QTableWidget::viewportEvent(event);
}

void DesktopSelectTable::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) {
        QTableWidget::mousePressEvent(e);
        return;
    }

    QModelIndex idx = indexAt(e->pos());
    if (idx.isValid() && selectionModel()->isRowSelected(idx.row(), QModelIndex())
        && !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        m_pendingRowDrag = true;
        m_rowDragMode = false;
        m_dragSourceRow = idx.row();
        m_origin = e->pos();
        m_dragActive = false;
        return;
    }

    m_pendingRowDrag = false;
    m_rowDragMode = false;
    m_dragActive = true;
    m_origin = e->pos();
    if (!m_band)
        m_band = new QRubberBand(QRubberBand::Rectangle, viewport());
    m_band->setGeometry(QRect(m_origin, QSize()));
    m_band->show();

    if (e->modifiers() & Qt::ControlModifier) {
        m_mode = Mode::Toggle;
        m_preSelection = selectionModel()->selection();
    } else if (e->modifiers() & Qt::ShiftModifier) {
        m_mode = Mode::Extend;
        m_preSelection = selectionModel()->selection();
    } else {
        m_mode = Mode::Replace;
        m_preSelection.clear();
        clearSelection();
    }

    if (idx.isValid()) {
        selectionModel()->select(model()->index(idx.row(), 0),
            (m_mode == Mode::Toggle ? QItemSelectionModel::Toggle : QItemSelectionModel::Select)
                | QItemSelectionModel::Rows);
    }
    killCurrentIndex();
}

void DesktopSelectTable::mouseMoveEvent(QMouseEvent* e) {
    updateHoverRow(e->pos());
    restartToolTipTimer(indexAt(e->pos()), e->pos());

    if (m_pendingRowDrag) {
        if ((e->pos() - m_origin).manhattanLength() <= QApplication::startDragDistance())
            return;
        m_pendingRowDrag = false;
        m_rowDragMode = true;
        setCursor(Qt::ClosedHandCursor);
    }

    if (m_rowDragMode) {
        int target = dropTargetRow(e->pos());
        if (!m_dropLine) {
            m_dropLine = new QFrame(viewport());
            m_dropLine->setFixedHeight(2);
            m_dropLine->setStyleSheet("background-color: #0078d4;");
        }
        int y = 0;
        if (target < rowCount()) {
            y = visualRect(model()->index(target, 0)).top();
        } else if (rowCount() > 0) {
            y = visualRect(model()->index(rowCount() - 1, 0)).bottom() + 1;
        }
        m_dropLine->setGeometry(0, y - 1, viewport()->width(), 2);
        m_dropLine->show();
        return;
    }

    if (!m_dragActive || !m_band)
        return;

    QRect rect(QPoint(m_origin), e->pos());
    rect = rect.normalized();
    m_band->setGeometry(rect);

    QItemSelection bandSelection;
    for (int row = 0; row < rowCount(); ++row) {
        QModelIndex first = model()->index(row, 0);
        QRect rowRect = visualRect(first);
        rowRect.setLeft(0);
        rowRect.setRight(viewport()->width());
        if (!rect.intersects(rowRect))
            continue;
        bandSelection.select(first, model()->index(row, columnCount() - 1));
    }

    if (m_mode == Mode::Toggle || m_mode == Mode::Extend) {
        QItemSelection merged = m_preSelection;
        merged.merge(bandSelection, m_mode == Mode::Toggle ? QItemSelectionModel::Toggle : QItemSelectionModel::Select);
        selectionModel()->select(merged, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    } else {
        selectionModel()->select(bandSelection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    killCurrentIndex();
}

void DesktopSelectTable::mouseReleaseEvent(QMouseEvent* e) {
    Q_UNUSED(e);
    if (m_pendingRowDrag) {
        m_pendingRowDrag = false;
        killCurrentIndex();
        return;
    }

    if (m_rowDragMode) {
        m_rowDragMode = false;
        setCursor(Qt::ArrowCursor);
        if (m_dropLine)
            m_dropLine->hide();

        int insertBefore = dropTargetRow(e->pos());
        QList<int> selectedRows;
        if (selectionModel()) {
            for (const QModelIndex& idx : selectionModel()->selectedRows())
                selectedRows.append(idx.row());
            std::sort(selectedRows.begin(), selectedRows.end());
        }
        if (selectedRows.isEmpty())
            selectedRows.append(m_dragSourceRow);
        m_dragSourceRow = -1;

        bool needMove = selectedRows.size() != 1
            || (insertBefore != selectedRows[0] && insertBefore != selectedRows[0] + 1);
        if (needMove && onRowsMoved) {
            onRowsMoved(selectedRows, insertBefore);
        } else if (needMove && selectedRows.size() == 1 && onRowMoved) {
            int from = selectedRows[0];
            onRowMoved(from, insertBefore > from ? insertBefore - 1 : insertBefore);
        }
        return;
    }

    if (m_dragActive && m_band) {
        m_band->hide();
        m_dragActive = false;
    }
    killCurrentIndex();
}

void DesktopSelectTable::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        QModelIndex idx = indexAt(e->pos());
        if (idx.isValid() && onCellDoubleClicked) {
            onCellDoubleClicked(idx.row(), idx.column());
            return;
        }
    }
    QTableWidget::mouseDoubleClickEvent(e);
}

void DesktopSelectTable::leaveEvent(QEvent* e) {
    hideDelayedToolTip();
    if (m_hoverRow != -1) {
        int oldRow = m_hoverRow;
        m_hoverRow = -1;
        updateOpWidgetStyle(this, oldRow);
        viewport()->update();
    }
    QTableWidget::leaveEvent(e);
}

void DesktopSelectTable::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    QTableWidget::selectionChanged(selected, deselected);
    killCurrentIndex();
    auto updateRows = [this](const QItemSelection& selection) {
        for (const auto& range : selection) {
            for (int row = range.top(); row <= range.bottom(); ++row)
                updateOpWidgetStyle(this, row);
        }
    };
    updateRows(selected);
    updateRows(deselected);
}

void DesktopSelectTable::focusInEvent(QFocusEvent* e) {
    QTableWidget::focusInEvent(e);
    killCurrentIndex();
}

void DesktopSelectTable::killCurrentIndex() {
    if (selectionModel())
        selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
}

void DesktopSelectTable::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Delete && onDeletePressed) {
        auto rows = selectionModel() ? selectionModel()->selectedRows() : QModelIndexList{};
        if (!rows.isEmpty()) {
            QList<int> rowList;
            for (const QModelIndex& idx : rows)
                rowList.append(idx.row());
            std::sort(rowList.begin(), rowList.end());
            onDeletePressed(rowList);
            return;
        }
    }

    if (onKeyPressed) {
        auto rows = selectionModel() ? selectionModel()->selectedRows() : QModelIndexList{};
        if (rows.size() == 1)
            onKeyPressed(rows[0].row(), e->key());
    }
    QTableWidget::keyPressEvent(e);
}
