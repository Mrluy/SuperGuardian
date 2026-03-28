#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

// ---- 配置持久化、搜索辅助、列宽管理、导入导出重置 ----

static QJsonArray scheduleRulesToJson(const QList<ScheduleRule>& rules) {
    QJsonArray arr;
    for (const ScheduleRule& r : rules) {
        QJsonObject o;
        o["type"] = (r.type == ScheduleRule::Periodic) ? "periodic" : "fixed";
        o["intervalSecs"] = r.intervalSecs;
        o["fixedTime"] = r.fixedTime.toString("HH:mm:ss");
        QJsonArray days;
        for (int d : r.daysOfWeek) days.append(d);
        o["daysOfWeek"] = days;
        o["nextTrigger"] = r.nextTrigger.toString(Qt::ISODate);
        arr.append(o);
    }
    return arr;
}

static QList<ScheduleRule> jsonToScheduleRules(const QJsonArray& arr) {
    QList<ScheduleRule> rules;
    for (const QJsonValue& v : arr) {
        QJsonObject o = v.toObject();
        ScheduleRule r;
        r.type = (o["type"].toString() == "fixed") ? ScheduleRule::FixedTime : ScheduleRule::Periodic;
        r.intervalSecs = o["intervalSecs"].toInt(3600);
        r.fixedTime = QTime::fromString(o["fixedTime"].toString(), "HH:mm:ss");
        QJsonArray days = o["daysOfWeek"].toArray();
        for (const QJsonValue& dv : days) r.daysOfWeek.insert(dv.toInt());
        r.nextTrigger = QDateTime::fromString(o["nextTrigger"].toString(), Qt::ISODate);
        rules.append(r);
    }
    return rules;
}

void SuperGuardian::loadSettings() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);

    // Load SMTP config
    s.beginGroup("smtp");
    smtpConfig.server = s.value("server").toString();
    smtpConfig.port = s.value("port", 587).toInt();
    smtpConfig.useTls = s.value("useTls", true).toBool();
    smtpConfig.username = s.value("username").toString();
    smtpConfig.password = s.value("password").toString();
    smtpConfig.fromAddress = s.value("fromAddress").toString();
    smtpConfig.fromName = s.value("fromName").toString();
    smtpConfig.toAddress = s.value("toAddress").toString();
    s.endGroup();

    if (emailEnabledAct) {
        emailEnabledAct->blockSignals(true);
        emailEnabledAct->setChecked(s.value("emailEnabled", false).toBool());
        emailEnabledAct->blockSignals(false);
    }

    int size = s.beginReadArray("items");
    for (int i = 0; i < size; ++i) {
        s.setArrayIndex(i);
        QString path = s.value("path").toString();
        GuardItem item;
        item.path = path;
        item.targetPath = resolveShortcut(path);
        item.processName = QFileInfo(item.targetPath).fileName();
        item.guarding = s.value("guard").toBool();
        item.startTime = QDateTime::fromString(s.value("startTime").toString());
        item.lastRestart = QDateTime::fromString(s.value("lastRestart").toString());
        item.restartCount = s.value("restartCount").toInt();
        item.startDelaySecs = s.value("startDelaySecs", 1).toInt();
        if (item.startDelaySecs < 1) item.startDelaySecs = 1;

        // Load schedule rules (new format)
        if (s.contains("restartRulesJson")) {
            QJsonDocument doc = QJsonDocument::fromJson(s.value("restartRulesJson").toString().toUtf8());
            item.restartRules = jsonToScheduleRules(doc.array());
        } else if (s.value("scheduledRestartIntervalSecs", 0).toInt() > 0) {
            // Backward compatibility: convert old single interval to rule
            ScheduleRule r;
            r.type = ScheduleRule::Periodic;
            r.intervalSecs = s.value("scheduledRestartIntervalSecs").toInt();
            r.nextTrigger = QDateTime::fromString(s.value("nextScheduledRestart").toString());
            item.restartRules.append(r);
            item.restartRulesActive = true;
        }
        item.restartRulesActive = s.value("restartRulesActive", !item.restartRules.isEmpty()).toBool();

        item.scheduledRunEnabled = s.value("scheduledRunEnabled", false).toBool();
        if (s.contains("runRulesJson")) {
            QJsonDocument doc = QJsonDocument::fromJson(s.value("runRulesJson").toString().toUtf8());
            item.runRules = jsonToScheduleRules(doc.array());
        }

        // Retry config
        item.retryConfig.retryIntervalSecs = s.value("retryIntervalSecs", 30).toInt();
        item.retryConfig.maxRetries = s.value("retryMaxRetries", 10).toInt();
        item.retryConfig.maxDurationSecs = s.value("retryMaxDurationSecs", 300).toInt();

        // Email notify per-program
        item.emailNotify.enabled = s.value("emailNotifyEnabled", false).toBool();
        item.emailNotify.onGuardTriggered = s.value("emailOnGuardTriggered", false).toBool();
        item.emailNotify.onStartFailed = s.value("emailOnStartFailed", true).toBool();
        item.emailNotify.onScheduledRestartFailed = s.value("emailOnRestartFailed", true).toBool();
        item.emailNotify.onScheduledRunFailed = s.value("emailOnRunFailed", true).toBool();
        item.emailNotify.onProcessExited = s.value("emailOnProcessExited", false).toBool();
        item.emailNotify.onRetryExhausted = s.value("emailOnRetryExhausted", true).toBool();

        items.append(item);
        int row = tableWidget->rowCount();
        tableWidget->insertRow(row);
        setupTableRow(row, item);
    }
    s.endArray();
    syncSelfGuardListEntry(selfGuardAct && selfGuardAct->isChecked());
}

void SuperGuardian::saveSettings() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);

    // Save SMTP config
    s.beginGroup("smtp");
    s.setValue("server", smtpConfig.server);
    s.setValue("port", smtpConfig.port);
    s.setValue("useTls", smtpConfig.useTls);
    s.setValue("username", smtpConfig.username);
    s.setValue("password", smtpConfig.password);
    s.setValue("fromAddress", smtpConfig.fromAddress);
    s.setValue("fromName", smtpConfig.fromName);
    s.setValue("toAddress", smtpConfig.toAddress);
    s.endGroup();

    if (emailEnabledAct)
        s.setValue("emailEnabled", emailEnabledAct->isChecked());

    s.beginWriteArray("items");
    int saveIndex = 0;
    for (int i = 0; i < items.size(); ++i) {
        s.setArrayIndex(saveIndex++);
        s.setValue("path", items[i].path);
        s.setValue("guard", items[i].guarding);
        s.setValue("lastRestart", items[i].lastRestart.toString());
        s.setValue("restartCount", items[i].restartCount);
        s.setValue("startTime", items[i].startTime.toString());
        s.setValue("startDelaySecs", items[i].startDelaySecs);

        QJsonDocument restartDoc(scheduleRulesToJson(items[i].restartRules));
        s.setValue("restartRulesJson", QString::fromUtf8(restartDoc.toJson(QJsonDocument::Compact)));
        s.setValue("restartRulesActive", items[i].restartRulesActive);

        s.setValue("scheduledRunEnabled", items[i].scheduledRunEnabled);
        QJsonDocument runDoc(scheduleRulesToJson(items[i].runRules));
        s.setValue("runRulesJson", QString::fromUtf8(runDoc.toJson(QJsonDocument::Compact)));

        s.setValue("retryIntervalSecs", items[i].retryConfig.retryIntervalSecs);
        s.setValue("retryMaxRetries", items[i].retryConfig.maxRetries);
        s.setValue("retryMaxDurationSecs", items[i].retryConfig.maxDurationSecs);

        s.setValue("emailNotifyEnabled", items[i].emailNotify.enabled);
        s.setValue("emailOnGuardTriggered", items[i].emailNotify.onGuardTriggered);
        s.setValue("emailOnStartFailed", items[i].emailNotify.onStartFailed);
        s.setValue("emailOnRestartFailed", items[i].emailNotify.onScheduledRestartFailed);
        s.setValue("emailOnRunFailed", items[i].emailNotify.onScheduledRunFailed);
        s.setValue("emailOnProcessExited", items[i].emailNotify.onProcessExited);
        s.setValue("emailOnRetryExhausted", items[i].emailNotify.onRetryExhausted);
    }
    s.endArray();
}

int SuperGuardian::findItemIndexByPath(const QString& path) const {
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].path == path) return i;
    }
    return -1;
}

int SuperGuardian::findRowByPath(const QString& path) const {
    for (int row = 0; row < tableWidget->rowCount(); ++row) {
        QTableWidgetItem* item = tableWidget->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == path) return row;
    }
    return -1;
}

QString SuperGuardian::rowPath(int row) const {
    if (row < 0 || row >= tableWidget->rowCount()) return QString();
    QTableWidgetItem* item = tableWidget->item(row, 0);
    if (!item) return QString();
    return item->data(Qt::UserRole).toString();
}

void SuperGuardian::clearListWithConfirmation() {
    if (!showMessageDialog(this, QString::fromUtf8("清空列表"), QString::fromUtf8("确认清空列表中的所有普通程序项吗？"), true)) {
        return;
    }

    for (int i = items.size() - 1; i >= 0; --i) {
        int row = findRowByPath(items[i].path);
        if (row >= 0) tableWidget->removeRow(row);
        items.removeAt(i);
    }
    saveSettings();
}

// ---- Column width management ----

void SuperGuardian::distributeColumnWidths() {
    if (!tableWidget) return;
    autoResizingColumns = true;
    int available = tableWidget->viewport()->width() - tableWidget->columnWidth(8);
    if (available <= 100) { autoResizingColumns = false; return; }

    const double defaultWeights[] = {3.0, 1.0, 1.5, 2.0, 1.0, 1.5, 2.0, 1.0};
    double ratios[8];
    bool hasCustom = false;

    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    if (s.contains("columnRatios")) {
        QStringList parts = s.value("columnRatios").toString().split(",");
        if (parts.size() == 8) {
            hasCustom = true;
            double sum = 0;
            for (int i = 0; i < 8; i++) { ratios[i] = parts[i].toDouble(); sum += ratios[i]; }
            if (sum <= 0.001) hasCustom = false;
            else for (int i = 0; i < 8; i++) ratios[i] /= sum;
        }
    }
    if (!hasCustom) {
        double sum = 0;
        for (int i = 0; i < 8; i++) { ratios[i] = defaultWeights[i]; sum += ratios[i]; }
        for (int i = 0; i < 8; i++) ratios[i] /= sum;
    }

    int remaining = available;
    for (int i = 0; i < 7; i++) {
        int w = qMax(40, (int)(available * ratios[i]));
        tableWidget->setColumnWidth(i, w);
        remaining -= w;
    }
    tableWidget->setColumnWidth(7, qMax(40, remaining));
    autoResizingColumns = false;
}

void SuperGuardian::saveColumnWidths() {
    if (autoResizingColumns) return;
    double total = 0;
    for (int i = 0; i < 8; i++) total += tableWidget->columnWidth(i);
    if (total <= 0) return;
    QStringList parts;
    for (int i = 0; i < 8; i++)
        parts.append(QString::number(tableWidget->columnWidth(i) / total, 'f', 6));
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("columnRatios", parts.join(","));
}

void SuperGuardian::resetColumnWidths() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.remove("columnRatios");
    distributeColumnWidths();
}

// ---- Config management ----

void SuperGuardian::exportConfig() {
    saveSettings();
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("SuperGuardian_Config_%1.ini").arg(timestamp);
    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u5bfc\u51fa\u914d\u7f6e"), defaultName, "INI Files (*.ini)");
    if (filePath.isEmpty()) return;
    if (QFile::exists(filePath)) QFile::remove(filePath);
    if (QFile::copy(appSettingsFilePath(), filePath)) {
        showMessageDialog(this, QString::fromUtf8("\u5bfc\u51fa\u914d\u7f6e"),
            QString::fromUtf8("\u914d\u7f6e\u5df2\u5bfc\u51fa\u5230\uff1a\n%1").arg(filePath));
    } else {
        showMessageDialog(this, QString::fromUtf8("\u5bfc\u51fa\u5931\u8d25"),
            QString::fromUtf8("\u65e0\u6cd5\u5199\u5165\u6587\u4ef6\uff1a%1").arg(filePath));
    }
}

void SuperGuardian::importConfig() {
    QString filePath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("\u5bfc\u5165\u914d\u7f6e"), "", "INI Files (*.ini)");
    if (filePath.isEmpty()) return;

    QSettings imported(filePath, QSettings::IniFormat);
    if (imported.status() != QSettings::NoError) {
        showMessageDialog(this, QString::fromUtf8("\u5bfc\u5165\u5931\u8d25"),
            QString::fromUtf8("\u914d\u7f6e\u6587\u4ef6\u683c\u5f0f\u65e0\u6548\u3002"));
        return;
    }
    int size = imported.beginReadArray("items");
    if (size < 0) {
        imported.endArray();
        showMessageDialog(this, QString::fromUtf8("\u5bfc\u5165\u5931\u8d25"),
            QString::fromUtf8("\u914d\u7f6e\u6587\u4ef6\u4e0d\u5305\u542b\u6709\u6548\u7684\u7a0b\u5e8f\u5217\u8868\u3002"));
        return;
    }
    for (int i = 0; i < size; i++) {
        imported.setArrayIndex(i);
        if (!imported.contains("path") || imported.value("path").toString().isEmpty()) {
            imported.endArray();
            showMessageDialog(this, QString::fromUtf8("\u5bfc\u5165\u5931\u8d25"),
                QString::fromUtf8("\u914d\u7f6e\u6587\u4ef6\u4e2d\u7b2c %1 \u4e2a\u7a0b\u5e8f\u9879\u7f3a\u5c11\u8def\u5f84\u4fe1\u606f\u3002").arg(i+1));
            return;
        }
    }
    imported.endArray();

    QFile::remove(appSettingsFilePath());
    QFile::copy(filePath, appSettingsFilePath());

    items.clear();
    tableWidget->setRowCount(0);
    loadSettings();
    applySavedTrayOptions();

    QSettings ss(appSettingsFilePath(), QSettings::IniFormat);
    QString theme = ss.contains("theme") ? ss.value("theme").toString() : detectSystemThemeName();
    applyTheme(theme);

    showMessageDialog(this, QString::fromUtf8("\u5bfc\u5165\u914d\u7f6e"),
        QString::fromUtf8("\u914d\u7f6e\u5df2\u6210\u529f\u5bfc\u5165\u3002"));
}

void SuperGuardian::resetConfig() {
    if (!showMessageDialog(this, QString::fromUtf8("\u91cd\u7f6e\u914d\u7f6e"),
        QString::fromUtf8("\u786e\u8ba4\u91cd\u7f6e\u5168\u90e8\u914d\u7f6e\u5417\uff1f\u6b64\u64cd\u4f5c\u5c06\u6e05\u9664\u6240\u6709\u8bbe\u7f6e\u548c\u7a0b\u5e8f\u5217\u8868\u3002"), true))
        return;

    QFile::remove(appSettingsFilePath());

    items.clear();
    tableWidget->setRowCount(0);

    if (selfGuardAct) {
        selfGuardAct->blockSignals(true);
        selfGuardAct->setChecked(false);
        selfGuardAct->blockSignals(false);
    }
    if (autostartAct) {
        autostartAct->blockSignals(true);
        autostartAct->setChecked(false);
        autostartAct->blockSignals(false);
    }
    stopWatchdogHelper();
    setAutostart(false);

    distributeColumnWidths();
    saveSettings();

    QString theme = detectSystemThemeName();
    QSettings ss(appSettingsFilePath(), QSettings::IniFormat);
    ss.setValue("theme", theme);
    applyTheme(theme);

    showMessageDialog(this, QString::fromUtf8("\u91cd\u7f6e\u914d\u7f6e"),
        QString::fromUtf8("\u914d\u7f6e\u5df2\u91cd\u7f6e\u4e3a\u9ed8\u8ba4\u8bbe\u7f6e\u3002"));
}

void SuperGuardian::rebuildTableFromItems() {
    tableWidget->setRowCount(0);
    for (int i = 0; i < items.size(); ++i) {
        const GuardItem& item = items[i];
        int row = tableWidget->rowCount();
        tableWidget->insertRow(row);
        setupTableRow(row, item);
    }
}
