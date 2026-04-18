#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "EmailService.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

static QString emailTargetDisplayName(const GuardItem& item) {
    QString display = item.processName;
    if (!item.launchArgs.trimmed().isEmpty())
        display += u" "_s + item.launchArgs.trimmed();
    if (!item.note.trimmed().isEmpty())
        display = u"%1（%2）"_s.arg(item.note.trimmed(), display);
    return display;
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
        logOperation(u"设置重试配置"_s, programId(items[itemIdx].processName, items[itemIdx].launchArgs));
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

    int applyPage = 0;
    const int applyPageSize = 10;

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
        int total = names.size();
        int totalPages = (total + applyPageSize - 1) / applyPageSize;
        if (applyPage >= totalPages) applyPage = totalPages - 1;
        if (applyPage < 0) applyPage = 0;

        int startIdx = applyPage * applyPageSize;
        int endIdx = qMin(startIdx + applyPageSize, total);
        QString text = u"将同步到 %1 个程序"_s.arg(total);
        if (totalPages > 1)
            text += u"（第 %1/%2 页）"_s.arg(applyPage + 1).arg(totalPages);
        text += u"：\n"_s;
        for (int i = startIdx; i < endIdx; ++i)
            text += names[i] + u"\n"_s;
        applyInfoLbl->setText(text.trimmed());
    };

    QPushButton* prevPageBtn = new QPushButton(u"上一页"_s);
    QPushButton* nextPageBtn = new QPushButton(u"下一页"_s);
    prevPageBtn->setVisible(false);
    nextPageBtn->setVisible(false);

    auto updatePageButtons = [&]() {
        int total = extraTargetIds.size();
        int totalPages = (total + applyPageSize - 1) / applyPageSize;
        bool multiPage = totalPages > 1;
        prevPageBtn->setVisible(multiPage);
        nextPageBtn->setVisible(multiPage);
        prevPageBtn->setEnabled(applyPage > 0);
        nextPageBtn->setEnabled(applyPage < totalPages - 1);
    };

    QHBoxLayout* pageLay = new QHBoxLayout();
    pageLay->addWidget(prevPageBtn);
    pageLay->addWidget(nextPageBtn);
    pageLay->addStretch();
    lay->addLayout(pageLay);

    QObject::connect(prevPageBtn, &QPushButton::clicked, [&]() {
        if (applyPage > 0) { --applyPage; refreshApplyInfo(); updatePageButtons(); }
    });
    QObject::connect(nextPageBtn, &QPushButton::clicked, [&]() {
        int totalPages = (static_cast<int>(extraTargetIds.size()) + applyPageSize - 1) / applyPageSize;
        if (applyPage < totalPages - 1) { ++applyPage; refreshApplyInfo(); updatePageButtons(); }
    });

    QObject::connect(applyOtherBtn, &QPushButton::clicked, [&]() {
        QDialog pickDlg(&dlg, kDialogFlags);
        pickDlg.setWindowTitle(u"应用到其它程序"_s);
        pickDlg.setMinimumSize(420, 360);

        QVBoxLayout* pickLay = new QVBoxLayout(&pickDlg);
        pickLay->addWidget(new QLabel(u"勾选需要同步当前邮件提醒设置的其它程序："_s));

        QLineEdit* searchEdit = new QLineEdit(&pickDlg);
        searchEdit->setPlaceholderText(u"搜索程序..."_s);
        searchEdit->setClearButtonEnabled(true);
        pickLay->addWidget(searchEdit);

        QListWidget* list = new QListWidget(&pickDlg);
        list->setSpacing(0);
        list->setUniformItemSizes(true);
        pickLay->addWidget(list, 1);

        auto populateList = [&]() {
            for (const GuardItem& item : items) {
                if (sourceIds.contains(item.id))
                    continue;
                QString displayName = emailTargetDisplayName(item);
                QListWidgetItem* listItem = new QListWidgetItem(displayName, list);
                listItem->setFlags(listItem->flags() | Qt::ItemIsUserCheckable);
                listItem->setCheckState(extraTargetIds.contains(item.id) ? Qt::Checked : Qt::Unchecked);
                listItem->setData(Qt::UserRole, item.id);
            }
        };
        populateList();

        QObject::connect(searchEdit, &QLineEdit::textChanged, [&](const QString& text) {
            QString keyword = text.trimmed().toLower();
            for (int i = 0; i < list->count(); ++i) {
                QListWidgetItem* it = list->item(i);
                it->setHidden(!keyword.isEmpty() && !it->text().toLower().contains(keyword));
            }
        });

        QHBoxLayout* toolLay = new QHBoxLayout();
        QPushButton* selectAllBtn = new QPushButton(u"全选"_s);
        QPushButton* clearBtn = new QPushButton(u"清空"_s);
        toolLay->addWidget(selectAllBtn);
        toolLay->addWidget(clearBtn);
        toolLay->addStretch();
        pickLay->addLayout(toolLay);

        QObject::connect(selectAllBtn, &QPushButton::clicked, [&]() {
            for (int i = 0; i < list->count(); ++i)
                if (!list->item(i)->isHidden())
                    list->item(i)->setCheckState(Qt::Checked);
        });
        QObject::connect(clearBtn, &QPushButton::clicked, [&]() {
            for (int i = 0; i < list->count(); ++i)
                if (!list->item(i)->isHidden())
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
        applyPage = 0;
        refreshApplyInfo();
        updatePageButtons();
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
        logOperation(u"设置邮件提醒"_s, programId(items[itemIdx].processName, items[itemIdx].launchArgs));
    }
    for (const QString& id : extraTargetIds) {
        int itemIdx = findItemIndexById(id);
        if (itemIdx < 0) continue;
        applyEmailNotify(items[itemIdx].emailNotify);
        logOperation(u"设置邮件提醒"_s, programId(items[itemIdx].processName, items[itemIdx].launchArgs));
    }
    saveSettings();
}

// ---- SMTP 配置导入导出辅助 ----

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
        showMessageDialog(&dlg, u"导入成功"_s, u"邮件提醒配置已加载到当前对话框。点击「保存」后生效。"_s);
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
    QHBoxLayout* smtpBtnLay = new QHBoxLayout();
    smtpBtnLay->addStretch();
    QPushButton* smtpOkBtn = new QPushButton(u"保存"_s);
    QPushButton* smtpCancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(smtpOkBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(smtpCancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    smtpBtnLay->addWidget(smtpOkBtn); smtpBtnLay->addWidget(smtpCancelBtn); smtpBtnLay->addStretch();
    lay->addLayout(smtpBtnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    smtpConfig = currentDialogConfig();
    saveSettings();
}

// ---- 邮件发送服务 ----

static QString esc(const QString& s) {
    QString r = s;
    r.replace("'", "''");
    return r;
}

static QString buildPsScript(const SmtpConfig& config, const QString& subject, const QString& body) {
    QStringList p;
    p << "try {";
    p << QString("$smtp = New-Object System.Net.Mail.SmtpClient('%1', %2);").arg(esc(config.server)).arg(config.port);
    if (config.useTls)
        p << "$smtp.EnableSsl = $true;";
    if (!config.username.isEmpty())
        p << QString("$smtp.Credentials = New-Object System.Net.NetworkCredential('%1', '%2');").arg(esc(config.username), esc(config.password));
    p << QString("$msg = New-Object System.Net.Mail.MailMessage;");
    p << QString("$msg.From = New-Object System.Net.Mail.MailAddress('%1', '%2');").arg(esc(config.fromAddress), esc(config.fromName));
    p << QString("$msg.To.Add('%1');").arg(esc(config.toAddress));
    p << QString("$msg.Subject = '%1';").arg(esc(subject));
    p << QString("$msg.Body = '%1';").arg(esc(body));
    p << "$msg.SubjectEncoding = [System.Text.Encoding]::UTF8;";
    p << "$msg.BodyEncoding = [System.Text.Encoding]::UTF8;";
    p << "$smtp.Send($msg);";
    p << "} catch { exit 1 }";
    return p.join(" ");
}

bool isSmtpConfigValid(const SmtpConfig& config) {
    return !config.server.isEmpty() && config.port > 0
        && !config.fromAddress.isEmpty() && !config.toAddress.isEmpty();
}

void sendNotificationAsync(const SmtpConfig& config, const QString& subject, const QString& body) {
    if (!isSmtpConfigValid(config)) return;
    QProcess* proc = new QProcess();
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc, &QProcess::deleteLater);
    // 超时自动终止，防止 PowerShell 挂起导致进程泄漏
    QTimer* killTimer = new QTimer(proc);
    killTimer->setSingleShot(true);
    QObject::connect(killTimer, &QTimer::timeout, proc, [proc]() {
        proc->kill();
    });
    killTimer->start(60000);
    proc->start("powershell", { "-NoProfile", "-NonInteractive", "-Command", buildPsScript(config, subject, body) });
}

bool sendTestEmail(const SmtpConfig& config) {
    if (!isSmtpConfigValid(config)) return false;
    QProcess proc;
    proc.start("powershell", { "-NoProfile", "-NonInteractive", "-Command",
        buildPsScript(config, u"SuperGuardian 测试邮件"_s, u"这是一封来自超级守护的测试邮件。"_s) });
    // 使用事件循环避免阻塞 UI 线程（阻塞会触发看门狗误判未响应）
    QElapsedTimer timer;
    timer.start();
    while (proc.state() != QProcess::NotRunning) {
        qApp->processEvents();
        if (proc.waitForFinished(100))
            break;
        if (timer.elapsed() > 30000) {
            proc.kill();
            proc.waitForFinished(1000);
            return false;
        }
    }
    return proc.exitCode() == 0;
}
