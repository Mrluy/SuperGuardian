#include "GuardTableWidgets.h"
#include <QPainter>
#include <QMouseEvent>

// --- BruteForceDelegate ---

void BruteForceDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
        painter->setPen(option.palette.highlightedText().color());
    } else {
        painter->fillRect(option.rect, option.palette.base());
        painter->setPen(option.palette.text().color());
    }
    QRect textRect = option.rect.adjusted(4, 0, -2, 0);
    QVariant deco = index.data(Qt::DecorationRole);
    if (!deco.isNull()) {
        QIcon icon = qvariant_cast<QIcon>(deco);
        if (!icon.isNull()) {
            int iconSz = qMin(option.rect.height() - 4, 20);
            QRect iconRect(option.rect.left() + 4, option.rect.top() + (option.rect.height() - iconSz) / 2, iconSz, iconSz);
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

void DesktopSelectTable::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
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

        QModelIndex idx = indexAt(e->pos());
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
    if (m_dragActive && m_band) {
        QRect rect = QRect(m_origin, e->pos()).normalized();
        m_band->setGeometry(rect);
        QItemSelection bandSel;
        for (int row = 0; row < rowCount(); ++row) {
            QRect rowRect = visualRect(model()->index(row, 0));
            rowRect.setLeft(0);
            rowRect.setRight(viewport()->width());
            if (rect.intersects(rowRect)) {
                QModelIndex first = model()->index(row, 0);
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
    if (m_dragActive && m_band) {
        m_band->hide();
        m_dragActive = false;
    }
    killCurrentIndex();
}

void DesktopSelectTable::focusInEvent(QFocusEvent* e) {
    QTableWidget::focusInEvent(e);
    killCurrentIndex();
}

void DesktopSelectTable::killCurrentIndex() {
    if (selectionModel())
        selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
}
