#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "EmailService.h"
#include "LogDatabase.h"
#include <QtWidgets>

static QString emailTargetDisplayName(const GuardItem& item) {
    if (!item.note.trimmed().isEmpty())
        return u"%1（%2）"_s.arg(item.note.trimmed(), item.processName);
    return item.processName;
}

// ---- 重试设置对话框 ----

void SuperGuardian::contextSetRetryConfig(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"重试设置"_s);
    dlg.setFixedWidth(360);
    dlg.setMinimumHeight(250);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"启动失败后的重试配置："_s));

    QFormLayout* form = new QFormLayout();
    QSpinBox* intervalSpin = new QSpinBox(); intervalSpin->setRange(5, 3600); intervalSpin->setValue(30); intervalSpin->setSuffix(u" 秒"_s);
    QSpinBox* maxRetriesSpin = new QSpinBox(); maxRetriesSpin->setRange(0, 9999); maxRetriesSpin->setValue(10);
    maxRetriesSpin->setSpecialValueText(u"无限制"_s);
    QSpinBox* maxDurSpin = new QSpinBox(); maxDurSpin->setRange(0, 86400); maxDurSpin->setValue(300); maxDurSpin->setSuffix(u" 秒"_s);
    maxDurSpin->setSpecialValueText(u"无限制"_s);
    form->addRow(u"重试间隔："_s, intervalSpin);
    form->addRow(u"最大重试次数："_s, maxRetriesSpin);
    form->addRow(u"最大重试时长："_s, maxDurSpin);
    lay->addLayout(form);

    if (rows.size() == 1) {
        int itemIdx = findItemIndexById(rowId(rows[0]));
        if (itemIdx >= 0) {
            intervalSpin->setValue(items[itemIdx].retryConfig.retryIntervalSecs);
            maxRetriesSpin->setValue(items[itemIdx].retryConfig.maxRetries);
            maxDurSpin->setValue(items[itemIdx].retryConfig.maxDurationSecs);
        }
    }

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

    for (int row : rows) {
        int itemIdx = findItemIndexById(rowId(row));
        if (itemIdx < 0) continue;
        items[itemIdx].retryConfig.retryIntervalSecs = intervalSpin->value();
        items[itemIdx].retryConfig.maxRetries = maxRetriesSpin->value();
        items[itemIdx].retryConfig.maxDurationSecs = maxDurSpin->value();
    }
    saveSettings();
}

// ---- 邮件提醒设置对话框 ----

void SuperGuardian::contextSetEmailNotify(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"邮件提醒设置"_s);
    dlg.setFixedWidth(360);
    dlg.setMinimumHeight(360);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QCheckBox* enabledCb = new QCheckBox(u"启用邮件提醒"_s);
    enabledCb->setChecked(true);
    lay->addWidget(enabledCb);
    lay->addWidget(new QLabel(u"提醒事件："_s));

    QCheckBox* cbGuardTriggered = new QCheckBox(u"守护触发重启"_s);
    QCheckBox* cbStartFailed = new QCheckBox(u"启动失败"_s);
    QCheckBox* cbRestartFailed = new QCheckBox(u"定时重启失败"_s);
    QCheckBox* cbRunFailed = new QCheckBox(u"定时运行失败"_s);
    QCheckBox* cbExited = new QCheckBox(u"进程退出"_s);
    QCheckBox* cbRetryExhausted = new QCheckBox(u"重试耗尽"_s);
    cbStartFailed->setChecked(true); cbRestartFailed->setChecked(true);
    cbRunFailed->setChecked(true); cbRetryExhausted->setChecked(true);

    lay->addWidget(cbGuardTriggered);
    lay->addWidget(cbStartFailed);
    lay->addWidget(cbRestartFailed);
    lay->addWidget(cbRunFailed);
    lay->addWidget(cbExited);
    lay->addWidget(cbRetryExhausted);

    if (rows.size() == 1) {
        int itemIdx = findItemIndexById(rowId(rows[0]));
        if (itemIdx >= 0) {
            const EmailNotifyConfig& en = items[itemIdx].emailNotify;
            enabledCb->setChecked(en.enabled);
            cbGuardTriggered->setChecked(en.onGuardTriggered);
            cbStartFailed->setChecked(en.onStartFailed);
            cbRestartFailed->setChecked(en.onScheduledRestartFailed);
            cbRunFailed->setChecked(en.onScheduledRunFailed);
            cbExited->setChecked(en.onProcessExited);
            cbRetryExhausted->setChecked(en.onRetryExhausted);
        }
    }

    QSet<QString> sourceIds;
    for (int row : rows) {
        QString id = rowId(row);
        if (!id.isEmpty())
            sourceIds.insert(id);
    }

    QSet<QString> extraTargetIds;
    QPushButton* applyOtherBtn = new QPushButton(u"应用到其它程序"_s);
    QLabel* applyInfoLbl = new QLabel(u"未选择其它程序"_s);
    applyInfoLbl->setWordWrap(true);
    lay->addSpacing(8);
    lay->addWidget(new QLabel(u"批量应用："_s));
    lay->addWidget(applyOtherBtn);
    lay->addWidget(applyInfoLbl);

    int otherProgramCount = 0;
    for (const GuardItem& item : items) {
        if (!sourceIds.contains(item.id))
            ++otherProgramCount;
    }
    applyOtherBtn->setEnabled(otherProgramCount > 0);
    if (otherProgramCount == 0)
        applyInfoLbl->setText(u"没有可应用的其它程序"_s);

    auto refreshApplyInfo = [&]() {
        if (extraTargetIds.isEmpty()) {
            applyInfoLbl->setText(otherProgramCount > 0 ? u"未选择其它程序"_s : u"没有可应用的其它程序"_s);
            return;
        }

        QStringList names;
        for (const QString& id : extraTargetIds) {
            int itemIdx = findItemIndexById(id);
            if (itemIdx >= 0)
                names.append(emailTargetDisplayName(items[itemIdx]));
        }
        applyInfoLbl->setText(u"将同步到 %1 个程序：%2"_s
            .arg(names.size())
            .arg(names.join(u"、"_s)));
    };

    QObject::connect(applyOtherBtn, &QPushButton::clicked, [&]() {
        QDialog pickDlg(&dlg, kDialogFlags);
        pickDlg.setWindowTitle(u"应用到其它程序"_s);
        pickDlg.setMinimumSize(420, 360);

        QVBoxLayout* pickLay = new QVBoxLayout(&pickDlg);
        pickLay->addWidget(new QLabel(u"勾选需要同步当前邮件提醒设置的其它程序："_s));

        QListWidget* list = new QListWidget(&pickDlg);
        pickLay->addWidget(list, 1);

        for (const GuardItem& item : items) {
            if (sourceIds.contains(item.id))
                continue;
            QListWidgetItem* listItem = new QListWidgetItem(emailTargetDisplayName(item), list);
            listItem->setFlags(listItem->flags() | Qt::ItemIsUserCheckable);
            listItem->setCheckState(extraTargetIds.contains(item.id) ? Qt::Checked : Qt::Unchecked);
            listItem->setData(Qt::UserRole, item.id);
        }

        QHBoxLayout* toolLay = new QHBoxLayout();
        QPushButton* selectAllBtn = new QPushButton(u"全选"_s);
        QPushButton* clearBtn = new QPushButton(u"清空"_s);
        toolLay->addWidget(selectAllBtn);
        toolLay->addWidget(clearBtn);
        toolLay->addStretch();
        pickLay->addLayout(toolLay);

        QObject::connect(selectAllBtn, &QPushButton::clicked, [&]() {
            for (int i = 0; i < list->count(); ++i)
                list->item(i)->setCheckState(Qt::Checked);
        });
        QObject::connect(clearBtn, &QPushButton::clicked, [&]() {
            for (int i = 0; i < list->count(); ++i)
                list->item(i)->setCheckState(Qt::Unchecked);
        });

        QHBoxLayout* pickBtnLay = new QHBoxLayout();
        pickBtnLay->addStretch();
        QPushButton* okBtn = new QPushButton(u"确定"_s);
        QPushButton* cancelBtn = new QPushButton(u"取消"_s);
        QObject::connect(okBtn, &QPushButton::clicked, &pickDlg, &QDialog::accept);
        QObject::connect(cancelBtn, &QPushButton::clicked, &pickDlg, &QDialog::reject);
        pickBtnLay->addWidget(okBtn);
        pickBtnLay->addWidget(cancelBtn);
        pickBtnLay->addStretch();
        pickLay->addLayout(pickBtnLay);

        if (pickDlg.exec() != QDialog::Accepted)
            return;

        extraTargetIds.clear();
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* listItem = list->item(i);
            if (listItem->checkState() == Qt::Checked)
                extraTargetIds.insert(listItem->data(Qt::UserRole).toString());
        }
        refreshApplyInfo();
    });

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

    auto applyEmailNotify = [&](EmailNotifyConfig& en) {
        en.enabled = enabledCb->isChecked();
        en.onGuardTriggered = cbGuardTriggered->isChecked();
        en.onStartFailed = cbStartFailed->isChecked();
        en.onScheduledRestartFailed = cbRestartFailed->isChecked();
        en.onScheduledRunFailed = cbRunFailed->isChecked();
        en.onProcessExited = cbExited->isChecked();
        en.onRetryExhausted = cbRetryExhausted->isChecked();
    };

    for (int row : rows) {
        int itemIdx = findItemIndexById(rowId(row));
        if (itemIdx < 0) continue;
        applyEmailNotify(items[itemIdx].emailNotify);
    }
    for (const QString& id : extraTargetIds) {
        int itemIdx = findItemIndexById(id);
        if (itemIdx < 0) continue;
        applyEmailNotify(items[itemIdx].emailNotify);
    }
    saveSettings();
}
