#pragma once

#include <QTableWidget>
#include <QStyledItemDelegate>
#include <QRubberBand>
#include <QItemSelection>
#include <QFrame>
#include <functional>

class BruteForceDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void drawFocus(QPainter*, const QStyleOptionViewItem&, const QRect&) const {}
};

class DesktopSelectTable : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
    std::function<void(int, int)> onRowMoved;
protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
private:
    void killCurrentIndex();
    int dropTargetRow(const QPoint& pos);
    enum class Mode { Replace, Toggle, Extend };
    Mode m_mode = Mode::Replace;
    bool m_dragActive = false;
    bool m_pendingRowDrag = false;
    bool m_rowDragMode = false;
    int m_dragSourceRow = -1;
    QPoint m_origin;
    QRubberBand* m_band = nullptr;
    QFrame* m_dropLine = nullptr;
    QItemSelection m_preSelection;
};
