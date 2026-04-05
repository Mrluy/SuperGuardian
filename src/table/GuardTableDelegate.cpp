#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include <QPainter>

using namespace Qt::Literals::StringLiterals;

void BruteForceDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    bool isDark = currentThemeName() == "dark";
    int hoverRow = -1;
    if (const QWidget* widget = option.widget) {
        if (auto* table = dynamic_cast<const DesktopSelectTable*>(widget)) {
            hoverRow = table->hoveredRow();
        } else if (auto* table = dynamic_cast<const DesktopSelectTable*>(widget->parentWidget())) {
            hoverRow = table->hoveredRow();
        }
    }

    bool isHovered = index.row() == hoverRow;
    bool isSelected = option.state & QStyle::State_Selected;
    if (isSelected) {
        painter->fillRect(option.rect, isDark ? QColor(0x21, 0x46, 0x6f) : QColor(0xdb, 0xea, 0xfe));
        painter->fillRect(QRect(option.rect.left(), option.rect.bottom() - 1, option.rect.width(), 2),
            isDark ? QColor(0x60, 0xcd, 0xff) : QColor(0x00, 0x5f, 0xb7));
    } else if (isHovered) {
        painter->fillRect(option.rect, isDark ? QColor(0x39, 0x39, 0x39) : QColor(0xf5, 0xf5, 0xf5));
    } else if (QVariant bg = index.data(Qt::BackgroundRole); bg.isValid()) {
        painter->fillRect(option.rect, qvariant_cast<QBrush>(bg));
    } else {
        painter->fillRect(option.rect, option.palette.base());
    }
    painter->setPen(isSelected && isDark ? QColor(Qt::white) : option.palette.text().color());

    if (QVariant font = index.data(Qt::FontRole); font.isValid())
        painter->setFont(qvariant_cast<QFont>(font));

    QRect textRect = option.rect.adjusted(4, 0, -2, 0);
    if (index.column() == 1 && index.data(Qt::UserRole + 2).toBool()) {
        QString pinRes = isDark ? u":/SuperGuardian/top_light.png"_s : u":/SuperGuardian/top_dark.png"_s;
        QIcon pinIcon(pinRes);
        if (!pinIcon.isNull()) {
            int pinSize = qMin(option.rect.height() - 4, 16);
            QRect pinRect(textRect.left(), option.rect.top() + (option.rect.height() - pinSize) / 2, pinSize, pinSize);
            pinIcon.paint(painter, pinRect);
            textRect.setLeft(pinRect.right() + 4);
        }
    }

    QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (!icon.isNull()) {
        int iconSize = qMin(option.rect.height() - 4, 20);
        QRect iconRect(textRect.left(), option.rect.top() + (option.rect.height() - iconSize) / 2, iconSize, iconSize);
        icon.paint(painter, iconRect);
        textRect.setLeft(iconRect.right() + 4);
    }

    int alignment = index.data(Qt::TextAlignmentRole).toInt();
    if (alignment == 0)
        alignment = Qt::AlignLeft | Qt::AlignVCenter;
    painter->drawText(textRect, alignment, index.data(Qt::DisplayRole).toString());
    painter->restore();
}
