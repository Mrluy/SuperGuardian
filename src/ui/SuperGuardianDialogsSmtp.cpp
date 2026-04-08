#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "EmailService.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::Literals::StringLiterals;

static QJsonObject smtpConfigToJsonObject(const SmtpConfig& config) {
    QJsonObject o;
    o[u"server"_s] = config.server;
    o[u"port"_s] = config.port;
    o[u"useTls"_s] = config.useTls;
    o[u"username"_s] = config.username;
    o[u"password"_s] = config.password;
    o[u"fromAddress"_s] = config.fromAddress;
    o[u"fromName"_s] = config.fromName;
    o[u"toAddress"_s] = config.toAddress;
    return o;
}

static bool tryParseSmtpConfig(const QJsonObject& root, SmtpConfig& config) {
    QJsonObject smtpObj = root;
    if (root.contains(u"smtp"_s) && root[u"smtp"_s].isObject())
        smtpObj = root[u"smtp"_s].toObject();
    if (smtpObj.isEmpty()) return false;
    if (!smtpObj.contains(u"server"_s) && !smtpObj.contains(u"username"_s)
        && !smtpObj.contains(u"fromAddress"_s) && !smtpObj.contains(u"toAddress"_s))
        return false;

    config.server = smtpObj[u"server"_s].toString();
    config.port = smtpObj.contains(u"port"_s) ? smtpObj[u"port"_s].toInt(587) : 587;
    config.useTls = smtpObj.contains(u"useTls"_s) ? smtpObj[u"useTls"_s].toBool(true) : true;
    config.username = smtpObj[u"username"_s].toString();
    config.password = smtpObj[u"password"_s].toString();
    config.fromAddress = smtpObj[u"fromAddress"_s].toString();
    config.fromName = smtpObj[u"fromName"_s].toString();
    config.toAddress = smtpObj[u"toAddress"_s].toString();
    return true;
}

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

    auto currentDialogConfig = [&]() {
        SmtpConfig cfg;
        cfg.server = serverEdit->text().trimmed();
        cfg.port = portSpin->value();
        cfg.useTls = tlsCb->isChecked();
        cfg.username = userEdit->text().trimmed();
        cfg.password = passEdit->text();
        cfg.fromAddress = fromEdit->text().trimmed();
        cfg.fromName = fromNameEdit->text().trimmed();
        cfg.toAddress = toEdit->text().trimmed();
        return cfg;
    };
    auto applyDialogConfig = [&](const SmtpConfig& cfg) {
        serverEdit->setText(cfg.server);
        portSpin->setValue(cfg.port);
        tlsCb->setChecked(cfg.useTls);
        userEdit->setText(cfg.username);
        passEdit->setText(cfg.password);
        fromEdit->setText(cfg.fromAddress);
        fromNameEdit->setText(cfg.fromName);
        toEdit->setText(cfg.toAddress);
    };

    QHBoxLayout* toolsLay = new QHBoxLayout();
    QPushButton* importBtn = new QPushButton(u"导入配置"_s);
    QPushButton* exportBtn = new QPushButton(u"导出配置"_s);
    QPushButton* testBtn = new QPushButton(u"发送测试邮件"_s);
    toolsLay->addWidget(importBtn);
    toolsLay->addWidget(exportBtn);
    toolsLay->addStretch();
    toolsLay->addWidget(testBtn);
    lay->addLayout(toolsLay);

    QObject::connect(importBtn, &QPushButton::clicked, [&]() {
        QString filePath = QFileDialog::getOpenFileName(&dlg,
            u"导入邮件提醒配置"_s, QString(), u"JSON Files (*.json)"_s);
        if (filePath.isEmpty()) return;
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            showMessageDialog(&dlg, u"导入失败"_s, u"无法读取文件：%1"_s.arg(filePath));
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject()) {
            showMessageDialog(&dlg, u"导入失败"_s, u"文件格式无效。"_s);
            return;
        }
        SmtpConfig imported;
        if (!tryParseSmtpConfig(doc.object(), imported)) {
            showMessageDialog(&dlg, u"导入失败"_s, u"文件中未找到有效的邮件提醒配置。"_s);
            return;
        }
        applyDialogConfig(imported);
        showMessageDialog(&dlg, u"导入成功"_s, u"邮件提醒配置已加载到当前对话框。点击“保存”后生效。"_s);
        logOperation(u"导入邮件提醒配置从 %1"_s.arg(filePath));
    });

    QObject::connect(exportBtn, &QPushButton::clicked, [&]() {
        QString timestamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
        QString defaultName = u"SuperGuardian_SMTP_%1.json"_s.arg(timestamp);
        QString filePath = QFileDialog::getSaveFileName(&dlg,
            u"导出邮件提醒配置"_s, defaultName, u"JSON Files (*.json)"_s);
        if (filePath.isEmpty()) return;
        QJsonObject root;
        root[u"smtp"_s] = smtpConfigToJsonObject(currentDialogConfig());
        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly)) {
            showMessageDialog(&dlg, u"导出失败"_s, u"无法写入文件：%1"_s.arg(filePath));
            return;
        }
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
        showMessageDialog(&dlg, u"导出成功"_s, u"邮件提醒配置已导出到：\n%1"_s.arg(filePath));
        logOperation(u"导出邮件提醒配置到 %1"_s.arg(filePath));
    });

    QObject::connect(testBtn, &QPushButton::clicked, [&]() {
        SmtpConfig test = currentDialogConfig();
        testBtn->setEnabled(false);
        testBtn->setText(u"发送中..."_s);
        QApplication::processEvents();
        bool ok = sendTestEmail(test);
        testBtn->setEnabled(true);
        testBtn->setText(u"发送测试邮件"_s);
        showMessageDialog(&dlg, u"测试邮件"_s,
            ok ? u"测试邮件发送成功！"_s : u"测试邮件发送失败，请检查配置。"_s);
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

    smtpConfig = currentDialogConfig();
    saveSettings();
}
