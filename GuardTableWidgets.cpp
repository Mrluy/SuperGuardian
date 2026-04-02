#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>

using namespace Qt::Literals::StringLiterals;

// --- ChineseContextMenuFilter ---

bool ChineseContextMenuFilter::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() != QEvent::ContextMenu) return false;
    QLineEdit* le = qobject_cast<QLineEdit*>(obj);
    QAbstractSpinBox* sb = nullptr;
    if (!le) {
        sb = qobject_cast<QAbstractSpinBox*>(obj);
        if (sb) le = sb->findChild<QLineEdit*>();
    }
    if (!le) return false;
    QContextMenuEvent* ce = static_cast<QContextMenuEvent*>(event);
    QMenu* m = le->createStandardContextMenu();
    if (!m) return false;
    for (QAction* a : m->actions()) {
        if (a->isSeparator()) continue;
        QString t = a->text();
        int tab = t.indexOf(QLatin1Char('\t'));
        QString label = tab >= 0 ? t.left(tab) : t;
        QString shortcut = tab >= 0 ? t.mid(tab) : QString();
        if (label == "&Undo") label = u"撤销"_s;
        else if (label == "&Redo") label = u"重做"_s;
        else if (label == "Cu&t") label = u"剪切"_s;
        else if (label == "&Copy") label = u"复制"_s;
        else if (label == "&Paste") label = u"粘贴"_s;
        else if (label == "Delete") label = u"删除"_s;
        else if (label == "Select All") label = u"全选"_s;
        else continue;
        a->setText(label + shortcut);
    }
    if (!sb) sb = qobject_cast<QAbstractSpinBox*>(le->parentWidget());
    if (sb) {
        m->addSeparator();
        QAction* upAct = m->addAction(u"增大"_s);
        QAction* downAct = m->addAction(u"减小"_s);
        QObject::connect(upAct, &QAction::triggered, sb, &QAbstractSpinBox::stepUp);
        QObject::connect(downAct, &QAction::triggered, sb, &QAbstractSpinBox::stepDown);
    }
    m->exec(ce->globalPos());
    delete m;
    return true;
}

// --- BruteForceDelegate ---

void BruteForceDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, QColor("#005fb8"));
        painter->setPen(Qt::white);
    } else {
        QVariant bgVar = index.data(Qt::BackgroundRole);
        if (bgVar.isValid())
            painter->fillRect(option.rect, qvariant_cast<QBrush>(bgVar));
        else
            painter->fillRect(option.rect, option.palette.base());
        painter->setPen(option.palette.text().color());
    }
    QVariant fontVar = index.data(Qt::FontRole);
    if (fontVar.isValid()) painter->setFont(qvariant_cast<QFont>(fontVar));
    QRect textRect = option.rect.adjusted(4, 0, -2, 0);

    // Draw pin icon before program icon (column 0 only)
    bool isPinned = index.data(Qt::UserRole + 2).toBool();
    if (index.column() == 0 && isPinned) {
        QString pinRes = (currentThemeName() == "dark")
            ? u":/SuperGuardian/top_light.png"_s
            : u":/SuperGuardian/top_dark.png"_s;
        QIcon pinIcon(pinRes);
        if (!pinIcon.isNull()) {
            int pinSz = qMin(option.rect.height() - 4, 16);
            QRect pinRect(textRect.left(), option.rect.top() + (option.rect.height() - pinSz) / 2, pinSz, pinSz);
            pinIcon.paint(painter, pinRect);
            textRect.setLeft(pinRect.right() + 4);
        }
    }

    QVariant deco = index.data(Qt::DecorationRole);
    if (!deco.isNull()) {
        QIcon icon = qvariant_cast<QIcon>(deco);
        if (!icon.isNull()) {
            int iconSz = qMin(option.rect.height() - 4, 20);
            QRect iconRect(textRect.left(), option.rect.top() + (option.rect.height() - iconSz) / 2, iconSz, iconSz);
            icon.paint(painter, iconRect);
            textRect.setLeft(iconRect.right() + 4);
        }
    }
    QString text = index.data(Qt::DisplayRole).toString();
    int alignment = index.data(Qt::TextAlignmentRole).toInt();
    if (alignment == 0) alignment = Qt::AlignLeft | Qt::AlignVCenter;
    painter->drawText(textRect, alignment, text);
    painter->restore();
}

// --- DesktopSelectTable ---

int DesktopSelectTable::dropTargetRow(const QPoint& pos) {
    int rc = rowCount();
    if (rc == 0) return 0;
    for (int r = 0; r < rc; r++) {
        QRect rr = visualRect(model()->index(r, 0));
        rr.setLeft(0);
        rr.setRight(viewport()->width());
        if (pos.y() < rr.center().y()) return r;
    }
    return rc;
}

void DesktopSelectTable::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        QModelIndex idx = indexAt(e->pos());

        // Check if clicking on an already-selected row (for drag-reorder)
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
            if (m_mode == Mode::Toggle)
                selectionModel()->select(model()->index(idx.row(), 0),
                    QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
            else
                selectionModel()->select(model()->index(idx.row(), 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
        killCurrentIndex();
    } else {
        QTableWidget::mousePressEvent(e);
    }
}

void DesktopSelectTable::mouseMoveEvent(QMouseEvent* e) {
    if (m_pendingRowDrag) {
        if ((e->pos() - m_origin).manhattanLength() > QApplication::startDragDistance()) {
            m_pendingRowDrag = false;
            m_rowDragMode = true;
            setCursor(Qt::ClosedHandCursor);
        } else {
            return;
        }
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
            QRect rr = visualRect(model()->index(target, 0));
            y = rr.top();
        } else if (rowCount() > 0) {
            QRect rr = visualRect(model()->index(rowCount() - 1, 0));
            y = rr.bottom() + 1;
        }
        m_dropLine->setGeometry(0, y - 1, viewport()->width(), 2);
        m_dropLine->show();
        return;
    }

    if (m_dragActive && m_band) {
        QRect rect = QRect(m_origin, e->pos()).normalized();
        m_band->setGeometry(rect);
        QItemSelection bandSel;
        for (int row = 0; row < rowCount(); ++row) {
            QModelIndex first = model()->index(row, 0);
            QRect rowRect = visualRect(first);
            rowRect.setLeft(0);
            rowRect.setRight(viewport()->width());
            if (rect.intersects(rowRect)) {
                QModelIndex last = model()->index(row, columnCount() - 1);
                bandSel.select(first, last);
            }
        }
        if (m_mode == Mode::Toggle) {
            QItemSelection merged = m_preSelection;
            merged.merge(bandSel, QItemSelectionModel::Toggle);
            selectionModel()->select(merged, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else if (m_mode == Mode::Extend) {
            QItemSelection merged = m_preSelection;
            merged.merge(bandSel, QItemSelectionModel::Select);
            selectionModel()->select(merged, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            selectionModel()->select(bandSel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        killCurrentIndex();
    }
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
        if (m_dropLine) m_dropLine->hide();
        int insertBefore = dropTargetRow(e->pos());
        // 多选拖动
        QList<int> selectedRows;
        if (selectionModel()) {
            auto idxList = selectionModel()->selectedRows();
            for (const auto& idx : idxList) selectedRows.append(idx.row());
            std::sort(selectedRows.begin(), selectedRows.end());
        }
        if (selectedRows.isEmpty()) selectedRows.append(m_dragSourceRow);
        m_dragSourceRow = -1;
        // 判断是否需要移动
        bool needMove = false;
        if (selectedRows.size() == 1) {
            int from = selectedRows[0];
            if (insertBefore != from && insertBefore != from + 1) needMove = true;
        } else {
            needMove = true;
        }
        if (needMove && onRowsMoved) {
            onRowsMoved(selectedRows, insertBefore);
        } else if (needMove && selectedRows.size() == 1 && onRowMoved) {
            int from = selectedRows[0];
            int toRow = (insertBefore > from) ? insertBefore - 1 : insertBefore;
            onRowMoved(from, toRow);
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
        if (idx.isValid() && onCellDoubleClicked)
            onCellDoubleClicked(idx.row(), idx.column());
        return;
    }
    QTableWidget::mouseDoubleClickEvent(e);
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
    if (e->key() == Qt::Key_Delete) {
        if (onDeletePressed) {
            auto rows = selectionModel() ? selectionModel()->selectedRows() : QModelIndexList{};
            if (!rows.isEmpty()) {
                QList<int> rowList;
                for (const auto& idx : rows) rowList.append(idx.row());
                std::sort(rowList.begin(), rowList.end());
                onDeletePressed(rowList);
                return;
            }
        }
    }
    if (onKeyPressed) {
        auto rows = selectionModel() ? selectionModel()->selectedRows() : QModelIndexList{};
        if (rows.size() == 1) {
            onKeyPressed(rows[0].row(), e->key());
        }
    }
    QTableWidget::keyPressEvent(e);
}
