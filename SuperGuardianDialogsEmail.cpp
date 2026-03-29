#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "EmailService.h"
#include <QtWidgets>

// ---- 重试设置对话框 ----

void SuperGuardian::contextSetRetryConfig(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u91cd\u8bd5\u8bbe\u7f6e"));
    dlg.setFixedWidth(360);
    dlg.setMinimumHeight(250);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u542f\u52a8\u5931\u8d25\u540e\u7684\u91cd\u8bd5\u914d\u7f6e\uff1a")));

    QFormLayout* form = new QFormLayout();
    QSpinBox* intervalSpin = new QSpinBox(); intervalSpin->setRange(5, 3600); intervalSpin->setValue(30); intervalSpin->setSuffix(QString::fromUtf8(" \u79d2"));
    QSpinBox* maxRetriesSpin = new QSpinBox(); maxRetriesSpin->setRange(0, 9999); maxRetriesSpin->setValue(10);
    maxRetriesSpin->setSpecialValueText(QString::fromUtf8("\u65e0\u9650\u5236"));
    QSpinBox* maxDurSpin = new QSpinBox(); maxDurSpin->setRange(0, 86400); maxDurSpin->setValue(300); maxDurSpin->setSuffix(QString::fromUtf8(" \u79d2"));
    maxDurSpin->setSpecialValueText(QString::fromUtf8("\u65e0\u9650\u5236"));
    form->addRow(QString::fromUtf8("\u91cd\u8bd5\u95f4\u9694\uff1a"), intervalSpin);
    form->addRow(QString::fromUtf8("\u6700\u5927\u91cd\u8bd5\u6b21\u6570\uff1a"), maxRetriesSpin);
    form->addRow(QString::fromUtf8("\u6700\u5927\u91cd\u8bd5\u65f6\u957f\uff1a"), maxDurSpin);
    lay->addLayout(form);

    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) {
            intervalSpin->setValue(items[itemIdx].retryConfig.retryIntervalSecs);
            maxRetriesSpin->setValue(items[itemIdx].retryConfig.maxRetries);
            maxDurSpin->setValue(items[itemIdx].retryConfig.maxDurationSecs);
        }
    }

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

    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
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
    dlg.setWindowTitle(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192\u8bbe\u7f6e"));
    dlg.setFixedWidth(360);
    dlg.setMinimumHeight(300);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QCheckBox* enabledCb = new QCheckBox(QString::fromUtf8("\u542f\u7528\u90ae\u4ef6\u63d0\u9192"));
    enabledCb->setChecked(true);
    lay->addWidget(enabledCb);
    lay->addWidget(new QLabel(QString::fromUtf8("\u63d0\u9192\u4e8b\u4ef6\uff1a")));

    QCheckBox* cbGuardTriggered = new QCheckBox(QString::fromUtf8("\u5b88\u62a4\u89e6\u53d1\u91cd\u542f"));
    QCheckBox* cbStartFailed = new QCheckBox(QString::fromUtf8("\u542f\u52a8\u5931\u8d25"));
    QCheckBox* cbRestartFailed = new QCheckBox(QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u5931\u8d25"));
    QCheckBox* cbRunFailed = new QCheckBox(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c\u5931\u8d25"));
    QCheckBox* cbExited = new QCheckBox(QString::fromUtf8("\u8fdb\u7a0b\u9000\u51fa"));
    QCheckBox* cbRetryExhausted = new QCheckBox(QString::fromUtf8("\u91cd\u8bd5\u8017\u5c3d"));
    cbStartFailed->setChecked(true); cbRestartFailed->setChecked(true);
    cbRunFailed->setChecked(true); cbRetryExhausted->setChecked(true);

    lay->addWidget(cbGuardTriggered);
    lay->addWidget(cbStartFailed);
    lay->addWidget(cbRestartFailed);
    lay->addWidget(cbRunFailed);
    lay->addWidget(cbExited);
    lay->addWidget(cbRetryExhausted);

    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
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

    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        EmailNotifyConfig& en = items[itemIdx].emailNotify;
        en.enabled = enabledCb->isChecked();
        en.onGuardTriggered = cbGuardTriggered->isChecked();
        en.onStartFailed = cbStartFailed->isChecked();
        en.onScheduledRestartFailed = cbRestartFailed->isChecked();
        en.onScheduledRunFailed = cbRunFailed->isChecked();
        en.onProcessExited = cbExited->isChecked();
        en.onRetryExhausted = cbRetryExhausted->isChecked();
    }
    saveSettings();
}

// ---- SMTP 邮件配置对话框 ----

void SuperGuardian::showSmtpConfigDialog() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192\u914d\u7f6e"));
    dlg.setMinimumSize(400, 420);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QFormLayout* form = new QFormLayout();
    QLineEdit* serverEdit = new QLineEdit(smtpConfig.server);
    QSpinBox* portSpin = new QSpinBox(); portSpin->setRange(1, 65535); portSpin->setValue(smtpConfig.port);
    QCheckBox* tlsCb = new QCheckBox(QString::fromUtf8("TLS/SSL")); tlsCb->setChecked(smtpConfig.useTls);
    QLineEdit* userEdit = new QLineEdit(smtpConfig.username);
    QLineEdit* passEdit = new QLineEdit(smtpConfig.password); passEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* fromEdit = new QLineEdit(smtpConfig.fromAddress);
    QLineEdit* fromNameEdit = new QLineEdit(smtpConfig.fromName);
    QLineEdit* toEdit = new QLineEdit(smtpConfig.toAddress);

    form->addRow(QString::fromUtf8("SMTP \u670d\u52a1\u5668\uff1a"), serverEdit);
    form->addRow(QString::fromUtf8("\u7aef\u53e3\uff1a"), portSpin);
    form->addRow(QString::fromUtf8("\u52a0\u5bc6\uff1a"), tlsCb);
    form->addRow(QString::fromUtf8("\u7528\u6237\u540d\uff1a"), userEdit);
    form->addRow(QString::fromUtf8("\u5bc6\u7801\uff1a"), passEdit);
    form->addRow(QString::fromUtf8("\u53d1\u4ef6\u4eba\u5730\u5740\uff1a"), fromEdit);
    form->addRow(QString::fromUtf8("\u53d1\u4ef6\u4eba\u540d\u79f0\uff1a"), fromNameEdit);
    form->addRow(QString::fromUtf8("\u6536\u4ef6\u4eba\u5730\u5740\uff1a"), toEdit);
    lay->addLayout(form);

    QPushButton* testBtn = new QPushButton(QString::fromUtf8("\u53d1\u9001\u6d4b\u8bd5\u90ae\u4ef6"));
    lay->addWidget(testBtn);

    QObject::connect(testBtn, &QPushButton::clicked, [&]() {
        SmtpConfig test;
        test.server = serverEdit->text();
        test.port = portSpin->value();
        test.useTls = tlsCb->isChecked();
        test.username = userEdit->text();
        test.password = passEdit->text();
        test.fromAddress = fromEdit->text();
        test.fromName = fromNameEdit->text();
        test.toAddress = toEdit->text();
        testBtn->setEnabled(false);
        testBtn->setText(QString::fromUtf8("\u53d1\u9001\u4e2d..."));
        QApplication::processEvents();
        bool ok = sendTestEmail(test);
        testBtn->setEnabled(true);
        testBtn->setText(QString::fromUtf8("\u53d1\u9001\u6d4b\u8bd5\u90ae\u4ef6"));
        showMessageDialog(&dlg, QString::fromUtf8("\u6d4b\u8bd5\u90ae\u4ef6"),
            ok ? QString::fromUtf8("\u6d4b\u8bd5\u90ae\u4ef6\u53d1\u9001\u6210\u529f\uff01")
               : QString::fromUtf8("\u6d4b\u8bd5\u90ae\u4ef6\u53d1\u9001\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u914d\u7f6e\u3002"));
    });

    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u4fdd\u5b58"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn); btnLay->addWidget(cancelBtn); btnLay->addStretch();
    lay->addLayout(btnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    smtpConfig.server = serverEdit->text();
    smtpConfig.port = portSpin->value();
    smtpConfig.useTls = tlsCb->isChecked();
    smtpConfig.username = userEdit->text();
    smtpConfig.password = passEdit->text();
    smtpConfig.fromAddress = fromEdit->text();
    smtpConfig.fromName = fromNameEdit->text();
    smtpConfig.toAddress = toEdit->text();
    saveSettings();
}
