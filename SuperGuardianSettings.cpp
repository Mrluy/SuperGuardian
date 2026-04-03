#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
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
    auto& db = ConfigDatabase::instance();

    // 首次启动时从旧版 INI 迁移
    db.migrateFromIni(appSettingsFilePath());

    // Load SMTP config
    smtpConfig.server = db.value(u"smtp/server"_s).toString();
    smtpConfig.port = db.value(u"smtp/port"_s, 587).toInt();
    smtpConfig.useTls = db.value(u"smtp/useTls"_s, true).toBool();
    smtpConfig.username = db.value(u"smtp/username"_s).toString();
    smtpConfig.password = db.value(u"smtp/password"_s).toString();
    smtpConfig.fromAddress = db.value(u"smtp/fromAddress"_s).toString();
    smtpConfig.fromName = db.value(u"smtp/fromName"_s).toString();
    smtpConfig.toAddress = db.value(u"smtp/toAddress"_s).toString();

    if (emailEnabledAct) {
        emailEnabledAct->blockSignals(true);
        emailEnabledAct->setChecked(db.value(u"emailEnabled"_s, false).toBool());
        emailEnabledAct->blockSignals(false);
    }

    // Load items from JSON array
    QString itemsJson = db.value(u"items"_s).toString();
    QJsonDocument doc = QJsonDocument::fromJson(itemsJson.toUtf8());
    QJsonArray arr = doc.array();

    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr[i].toObject();
        QString path = o[u"path"_s].toString();
        if (path.isEmpty()) continue;

        GuardItem item;
        item.path = path;
        QString shortcutArgs;
        item.targetPath = resolveShortcut(path, &shortcutArgs);
        item.launchArgs = o[u"launchArgs"_s].toString();
        if (item.launchArgs.isEmpty() && !shortcutArgs.isEmpty())
            item.launchArgs = shortcutArgs;
        item.processName = QFileInfo(item.targetPath).fileName();
        item.guarding = o[u"guard"_s].toBool();
        item.startTime = QDateTime::fromString(o[u"startTime"_s].toString());
        item.lastRestart = QDateTime::fromString(o[u"lastRestart"_s].toString());
        item.restartCount = 0;
        item.startDelaySecs = o.contains(u"startDelaySecs"_s) ? o[u"startDelaySecs"_s].toInt() : 1;
        if (item.startDelaySecs < 0) item.startDelaySecs = 0;

        // Load schedule rules
        if (o.contains(u"restartRulesJson"_s)) {
            QJsonDocument rulesDoc = QJsonDocument::fromJson(o[u"restartRulesJson"_s].toString().toUtf8());
            item.restartRules = jsonToScheduleRules(rulesDoc.array());
        } else if (o[u"scheduledRestartIntervalSecs"_s].toInt(0) > 0) {
            ScheduleRule r;
            r.type = ScheduleRule::Periodic;
            r.intervalSecs = o[u"scheduledRestartIntervalSecs"_s].toInt();
            r.nextTrigger = QDateTime::fromString(o[u"nextScheduledRestart"_s].toString());
            item.restartRules.append(r);
            item.restartRulesActive = true;
        }
        item.restartRulesActive = o.contains(u"restartRulesActive"_s)
            ? o[u"restartRulesActive"_s].toBool()
            : !item.restartRules.isEmpty();

        item.scheduledRunEnabled = o[u"scheduledRunEnabled"_s].toBool();
        item.trackRunDuration = o[u"trackRunDuration"_s].toBool();
        if (o.contains(u"runRulesJson"_s)) {
            QJsonDocument rulesDoc = QJsonDocument::fromJson(o[u"runRulesJson"_s].toString().toUtf8());
            item.runRules = jsonToScheduleRules(rulesDoc.array());
        }

        // Retry config
        item.retryConfig.retryIntervalSecs = o.contains(u"retryIntervalSecs"_s) ? o[u"retryIntervalSecs"_s].toInt() : 30;
        item.retryConfig.maxRetries = o.contains(u"retryMaxRetries"_s) ? o[u"retryMaxRetries"_s].toInt() : 10;
        item.retryConfig.maxDurationSecs = o.contains(u"retryMaxDurationSecs"_s) ? o[u"retryMaxDurationSecs"_s].toInt() : 300;

        // Email notify per-program
        item.emailNotify.enabled = o[u"emailNotifyEnabled"_s].toBool();
        item.emailNotify.onGuardTriggered = o[u"emailOnGuardTriggered"_s].toBool();
        item.emailNotify.onStartFailed = o.contains(u"emailOnStartFailed"_s) ? o[u"emailOnStartFailed"_s].toBool() : true;
        item.emailNotify.onScheduledRestartFailed = o.contains(u"emailOnRestartFailed"_s) ? o[u"emailOnRestartFailed"_s].toBool() : true;
        item.emailNotify.onScheduledRunFailed = o.contains(u"emailOnRunFailed"_s) ? o[u"emailOnRunFailed"_s].toBool() : true;
        item.emailNotify.onProcessExited = o[u"emailOnProcessExited"_s].toBool();
        item.emailNotify.onRetryExhausted = o.contains(u"emailOnRetryExhausted"_s) ? o[u"emailOnRetryExhausted"_s].toBool() : true;

        item.pinned = o[u"pinned"_s].toBool();
        item.note = o[u"note"_s].toString();
        item.insertionOrder = o.contains(u"insertionOrder"_s) ? o[u"insertionOrder"_s].toInt() : i;
        item.guardStartTime = QDateTime::fromString(o[u"guardStartTime"_s].toString());

        items.append(item);
    }

    // Load duplicate whitelist
    QString wl = db.value(u"duplicateWhitelist"_s).toString();
    duplicateWhitelist = wl.isEmpty() ? QStringList() : wl.split(u"|"_s);

    rebuildTableFromItems();
    syncSelfGuardListEntry(selfGuardAct && selfGuardAct->isChecked());
}

void SuperGuardian::saveSettings() {
    auto& db = ConfigDatabase::instance();
    db.beginBatch();

    // Save SMTP config
    db.setValue(u"smtp/server"_s, smtpConfig.server);
    db.setValue(u"smtp/port"_s, smtpConfig.port);
    db.setValue(u"smtp/useTls"_s, smtpConfig.useTls);
    db.setValue(u"smtp/username"_s, smtpConfig.username);
    db.setValue(u"smtp/password"_s, smtpConfig.password);
    db.setValue(u"smtp/fromAddress"_s, smtpConfig.fromAddress);
    db.setValue(u"smtp/fromName"_s, smtpConfig.fromName);
    db.setValue(u"smtp/toAddress"_s, smtpConfig.toAddress);

    if (emailEnabledAct)
        db.setValue(u"emailEnabled"_s, emailEnabledAct->isChecked());

    // Save items as JSON array
    QJsonArray itemsArr;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject o;
        o[u"path"_s] = items[i].path;
        o[u"launchArgs"_s] = items[i].launchArgs;
        o[u"guard"_s] = items[i].guarding;
        o[u"lastRestart"_s] = items[i].lastRestart.toString();
        o[u"restartCount"_s] = items[i].restartCount;
        o[u"startTime"_s] = items[i].startTime.toString();
        o[u"startDelaySecs"_s] = items[i].startDelaySecs;

        QJsonDocument restartDoc(scheduleRulesToJson(items[i].restartRules));
        o[u"restartRulesJson"_s] = QString::fromUtf8(restartDoc.toJson(QJsonDocument::Compact));
        o[u"restartRulesActive"_s] = items[i].restartRulesActive;

        o[u"scheduledRunEnabled"_s] = items[i].scheduledRunEnabled;
        o[u"trackRunDuration"_s] = items[i].trackRunDuration;
        QJsonDocument runDoc(scheduleRulesToJson(items[i].runRules));
        o[u"runRulesJson"_s] = QString::fromUtf8(runDoc.toJson(QJsonDocument::Compact));

        o[u"retryIntervalSecs"_s] = items[i].retryConfig.retryIntervalSecs;
        o[u"retryMaxRetries"_s] = items[i].retryConfig.maxRetries;
        o[u"retryMaxDurationSecs"_s] = items[i].retryConfig.maxDurationSecs;

        o[u"emailNotifyEnabled"_s] = items[i].emailNotify.enabled;
        o[u"emailOnGuardTriggered"_s] = items[i].emailNotify.onGuardTriggered;
        o[u"emailOnStartFailed"_s] = items[i].emailNotify.onStartFailed;
        o[u"emailOnRestartFailed"_s] = items[i].emailNotify.onScheduledRestartFailed;
        o[u"emailOnRunFailed"_s] = items[i].emailNotify.onScheduledRunFailed;
        o[u"emailOnProcessExited"_s] = items[i].emailNotify.onProcessExited;
        o[u"emailOnRetryExhausted"_s] = items[i].emailNotify.onRetryExhausted;

        o[u"pinned"_s] = items[i].pinned;
        o[u"note"_s] = items[i].note;
        o[u"insertionOrder"_s] = items[i].insertionOrder;
        o[u"guardStartTime"_s] = items[i].guardStartTime.toString();

        itemsArr.append(o);
    }
    db.setValue(u"items"_s, QString::fromUtf8(QJsonDocument(itemsArr).toJson(QJsonDocument::Compact)));

    db.endBatch();
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
    if (!showMessageDialog(this, u"清空列表"_s, u"确认清空列表中的所有项吗？"_s, true)) {
        return;
    }
    logOperation(u"清空列表"_s);

    for (int i = items.size() - 1; i >= 0; --i) {
        int row = findRowByPath(items[i].path);
        if (row >= 0) tableWidget->removeRow(row);
        items.removeAt(i);
    }
    rebuildTableFromItems();
    saveSettings();
}
