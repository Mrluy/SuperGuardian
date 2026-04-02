#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include <QtWidgets>

// ---- 启动延时设置对话框 ----

void SuperGuardian::contextSetStartDelay(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u8bbe\u7f6e\u542f\u52a8\u5ef6\u65f6"));
    dlg.setFixedWidth(320);
    dlg.setMinimumHeight(150);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u8bf7\u8bbe\u7f6e\u542f\u52a8\u5ef6\u65f6\uff08\u79d2\uff09\uff1a")));
    lay->addWidget(new QLabel(QString::fromUtf8("\u7a0b\u5e8f\u91cd\u542f\u65f6\u7684\u5ef6\u65f6\uff0c\u8bbe\u7f6e\u4e3a 0 \u5173\u95ed\u5ef6\u65f6\u3002\n\u5b88\u62a4\u91cd\u542f\u3001\u5b9a\u65f6\u91cd\u542f\u5747\u4f7f\u7528\u6b64\u5ef6\u65f6\u3002")));
    QSpinBox* spin = new QSpinBox();
    spin->setRange(0, 86400);
    spin->setValue(1);
    spin->setSuffix(QString::fromUtf8(" \u79d2"));
    spin->setSpecialValueText(QString::fromUtf8("\u5173\u95ed"));
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) spin->setValue(items[itemIdx].startDelaySecs);
    }
    lay->addWidget(spin);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
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
    dlg.setWindowTitle(QString::fromUtf8("\u8bbe\u7f6e\u542f\u52a8\u7a0b\u5e8f/\u53c2\u6570"));
    dlg.setFixedWidth(450);
    dlg.setMinimumHeight(200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(QString::fromUtf8("\u542f\u52a8\u7a0b\u5e8f\u8def\u5f84\uff1a")));
    QLineEdit* pathEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) pathEdit->setText(items[itemIdx].targetPath);
    }
    lay->addWidget(pathEdit);

    lay->addWidget(new QLabel(QString::fromUtf8("\u542f\u52a8\u53c2\u6570\uff08\u7559\u7a7a\u8868\u793a\u65e0\u53c2\u6570\uff09\uff1a")));
    QLineEdit* argsEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) argsEdit->setText(items[itemIdx].launchArgs);
    }
    lay->addWidget(argsEdit);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
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
