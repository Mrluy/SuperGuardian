#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include <QtWidgets>

// ---- 启动延时设置对话框 ----

void SuperGuardian::contextSetStartDelay(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"设置启动延时"_s);
    dlg.setFixedWidth(320);
    dlg.setMinimumHeight(150);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"请设置启动延时（秒）："_s));
    lay->addWidget(new QLabel(u"程序重启时的延时，设置为 0 关闭延时。\n守护重启、定时重启均使用此延时。"_s));
    QSpinBox* spin = new QSpinBox();
    spin->setRange(0, 86400);
    spin->setValue(1);
    spin->setSuffix(u" 秒"_s);
    spin->setSpecialValueText(u"关闭"_s);
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) spin->setValue(items[itemIdx].startDelaySecs);
    }
    lay->addWidget(spin);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    int delaySecs = spin->value();
    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        item.startDelaySecs = delaySecs;
        item.startDelayExitTime = QDateTime();
        logOperation(u"设置启动延时 %1秒"_s.arg(delaySecs), programId(item.processName, item.launchArgs));
        if (tableWidget->item(row, 8)) {
            if (item.scheduledRunEnabled)
                tableWidget->item(row, 8)->setText("-");
            else
                tableWidget->item(row, 8)->setText(formatStartDelay(delaySecs));
        }
    }
    saveSettings();
}

// ---- 启动程序/参数设置对话框 ----

void SuperGuardian::contextSetLaunchArgs(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"设置启动程序/参数"_s);
    dlg.setFixedWidth(450);
    dlg.setMinimumHeight(200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(u"启动程序路径："_s));
    QLineEdit* pathEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) pathEdit->setText(items[itemIdx].targetPath);
    }
    lay->addWidget(pathEdit);

    lay->addWidget(new QLabel(u"启动参数（留空表示无参数）："_s));
    QLineEdit* argsEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) argsEdit->setText(items[itemIdx].launchArgs);
    }
    lay->addWidget(argsEdit);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn); btnLay->addWidget(cancelBtn); btnLay->addStretch();
    lay->addLayout(btnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    QString newPath = pathEdit->text().trimmed();
    QString args = argsEdit->text().trimmed();
    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        if (!newPath.isEmpty() && newPath != item.targetPath) {
            item.targetPath = newPath;
            item.processName = QFileInfo(newPath).fileName();
        }
        item.launchArgs = args;
        logOperation(u"设置启动程序/参数"_s, programId(item.processName, args));
        QString displayName = item.note.isEmpty()
            ? (args.isEmpty() ? item.processName : (item.processName + " " + args))
            : item.note;
        QString tooltipName = args.isEmpty() ? item.processName : (item.processName + " " + args);
        if (tableWidget->item(row, 0)) {
            tableWidget->item(row, 0)->setText(displayName);
            tableWidget->item(row, 0)->setToolTip(tooltipName);
            tableWidget->item(row, 0)->setIcon(getFileIcon(item.targetPath));
        }
    }
    saveSettings();
}
