#pragma once

#include <QTableWidget>
#include <QStyledItemDelegate>
#include <QRubberBand>
#include <QItemSelection>

class BruteForceDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void drawFocus(QPainter*, const QStyleOptionViewItem&, const QRect&) const {}
};

class DesktopSelectTable : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
private:
    void killCurrentIndex();
    enum class Mode { Replace, Toggle, Extend };
    Mode m_mode = Mode::Replace;
    bool m_dragActive = false;
    QPoint m_origin;
    QRubberBand* m_band = nullptr;
    QItemSelection m_preSelection;
};
