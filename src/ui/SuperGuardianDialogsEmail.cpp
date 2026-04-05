#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "EmailService.h"
#include <QtWidgets>

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
    dlg.setMinimumHeight(300);
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
    dlg.setWindowTitle(u"邮件提醒配置"_s);
    dlg.setMinimumSize(400, 420);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QFormLayout* form = new QFormLayout();
    QLineEdit* serverEdit = new QLineEdit(smtpConfig.server);
    QSpinBox* portSpin = new QSpinBox(); portSpin->setRange(1, 65535); portSpin->setValue(smtpConfig.port);
    QCheckBox* tlsCb = new QCheckBox(u"TLS/SSL"_s); tlsCb->setChecked(smtpConfig.useTls);
    QLineEdit* userEdit = new QLineEdit(smtpConfig.username);
    QLineEdit* passEdit = new QLineEdit(smtpConfig.password); passEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* fromEdit = new QLineEdit(smtpConfig.fromAddress);
    QLineEdit* fromNameEdit = new QLineEdit(smtpConfig.fromName);
    QLineEdit* toEdit = new QLineEdit(smtpConfig.toAddress);

    form->addRow(u"SMTP 服务器："_s, serverEdit);
    form->addRow(u"端口："_s, portSpin);
    form->addRow(u"加密："_s, tlsCb);
    form->addRow(u"用户名："_s, userEdit);
    form->addRow(u"密码："_s, passEdit);
    form->addRow(u"发件人地址："_s, fromEdit);
    form->addRow(u"发件人名称："_s, fromNameEdit);
    form->addRow(u"收件人地址："_s, toEdit);
    lay->addLayout(form);

    QPushButton* testBtn = new QPushButton(u"发送测试邮件"_s);
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
        testBtn->setText(u"发送中..."_s);
        QApplication::processEvents();
        bool ok = sendTestEmail(test);
        testBtn->setEnabled(true);
        testBtn->setText(u"发送测试邮件"_s);
        showMessageDialog(&dlg, u"测试邮件"_s,
            ok ? u"测试邮件发送成功！"_s
               : u"测试邮件发送失败，请检查配置。"_s);
    });

    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"保存"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
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
