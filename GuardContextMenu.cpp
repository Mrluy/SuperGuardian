#include "GuardTableWidgets.h"

using namespace Qt::Literals::StringLiterals;

bool ChineseContextMenuFilter::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() != QEvent::ContextMenu)
        return false;

    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(obj);
    QAbstractSpinBox* spinBox = nullptr;
    if (!lineEdit) {
        spinBox = qobject_cast<QAbstractSpinBox*>(obj);
        if (spinBox)
            lineEdit = spinBox->findChild<QLineEdit*>();
    }
    if (!lineEdit)
        return false;

    QContextMenuEvent* ce = static_cast<QContextMenuEvent*>(event);
    QMenu* menu = lineEdit->createStandardContextMenu();
    if (!menu)
        return false;

    for (QAction* action : menu->actions()) {
        if (action->isSeparator())
            continue;
        QString text = action->text();
        int tab = text.indexOf(QLatin1Char('\t'));
        QString label = tab >= 0 ? text.left(tab) : text;
        QString shortcut = tab >= 0 ? text.mid(tab) : QString();
        if (label == "&Undo") label = u"撤销"_s;
        else if (label == "&Redo") label = u"重做"_s;
        else if (label == "Cu&t") label = u"剪切"_s;
        else if (label == "&Copy") label = u"复制"_s;
        else if (label == "&Paste") label = u"粘贴"_s;
        else if (label == "Delete") label = u"删除"_s;
        else if (label == "Select All") label = u"全选"_s;
        else continue;
        action->setText(label + shortcut);
    }

    if (!spinBox)
        spinBox = qobject_cast<QAbstractSpinBox*>(lineEdit->parentWidget());
    if (spinBox) {
        menu->addSeparator();
        QAction* upAct = menu->addAction(u"增大"_s);
        QAction* downAct = menu->addAction(u"减小"_s);
        QObject::connect(upAct, &QAction::triggered, spinBox, &QAbstractSpinBox::stepUp);
        QObject::connect(downAct, &QAction::triggered, spinBox, &QAbstractSpinBox::stepDown);
    }

    menu->exec(ce->globalPos());
    delete menu;
    return true;
}
