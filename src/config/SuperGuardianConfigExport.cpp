#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::Literals::StringLiterals;

static QJsonArray exportScheduleRules(const QList<ScheduleRule>& rules) {
    QJsonArray arr;
    for (const ScheduleRule& r : rules) {
        QJsonObject o;
        if (r.type == ScheduleRule::Advanced)
            o[u"type"_s] = u"advanced"_s;
        else
            o[u"type"_s] = (r.type == ScheduleRule::Periodic) ? u"periodic"_s : u"fixed"_s;
        o[u"intervalSecs"_s] = r.intervalSecs;
        o[u"fixedTime"_s] = r.fixedTime.toString(u"HH:mm:ss"_s);
        QJsonArray days;
        for (int d : r.daysOfWeek)
            days.append(d);
        o[u"daysOfWeek"_s] = days;
        o[u"nextTrigger"_s] = r.nextTrigger.toString(Qt::ISODate);
        if (r.type == ScheduleRule::Advanced) {
            o[u"advMinute"_s] = r.advMinute;
            o[u"advHour"_s] = r.advHour;
            o[u"advDay"_s] = r.advDay;
            o[u"advMonth"_s] = r.advMonth;
            o[u"advYear"_s] = r.advYear;
            QJsonArray advDow;
            for (int d : r.advDaysOfWeek) advDow.append(d);
            o[u"advDaysOfWeek"_s] = advDow;
        }
        arr.append(o);
    }
    return arr;
}

void SuperGuardian::exportConfig() {
    saveSettings();
    auto& db = ConfigDatabase::instance();

    QJsonObject root;
    root[u"alwaysOnTop"_s] = db.value(u"alwaysOnTop"_s, false).toBool();
    root[u"emailEnabled"_s] = db.value(u"emailEnabled"_s, false).toBool();
    root[u"theme"_s] = db.value(u"theme"_s, u"dark"_s).toString();

    QJsonObject smtpObj;
    smtpObj[u"server"_s] = smtpConfig.server;
    smtpObj[u"port"_s] = smtpConfig.port;
    smtpObj[u"useTls"_s] = smtpConfig.useTls;
    smtpObj[u"username"_s] = smtpConfig.username;
    smtpObj[u"password"_s] = smtpConfig.password;
    smtpObj[u"fromAddress"_s] = smtpConfig.fromAddress;
    smtpObj[u"fromName"_s] = smtpConfig.fromName;
    smtpObj[u"toAddress"_s] = smtpConfig.toAddress;
    root[u"smtp"_s] = smtpObj;

    QJsonArray itemsArr;
    for (const GuardItem& item : items) {
        QJsonObject o;
        o[u"id"_s] = item.id;
        o[u"path"_s] = item.path;
        o[u"note"_s] = item.note;
        o[u"pinned"_s] = item.pinned;
        o[u"insertionOrder"_s] = item.insertionOrder;
        o[u"scheduledRunEnabled"_s] = item.scheduledRunEnabled;
        o[u"startDelaySecs"_s] = item.startDelaySecs;
        o[u"trackRunDuration"_s] = item.trackRunDuration;
        if (!item.launchArgs.isEmpty())
            o[u"launchArgs"_s] = item.launchArgs;

        QJsonObject guardObj;
        guardObj[u"enabled"_s] = item.guarding;
        guardObj[u"startTime"_s] = item.guardStartTime.isValid()
            ? item.guardStartTime.toString(Qt::ISODate) : u""_s;
        o[u"guard"_s] = guardObj;

        QJsonObject retryObj;
        retryObj[u"intervalSecs"_s] = item.retryConfig.retryIntervalSecs;
        retryObj[u"maxDurationSecs"_s] = item.retryConfig.maxDurationSecs;
        retryObj[u"maxRetries"_s] = item.retryConfig.maxRetries;
        o[u"retry"_s] = retryObj;

        o[u"runRules"_s] = exportScheduleRules(item.runRules);
        o[u"restartRules"_s] = exportScheduleRules(item.restartRules);
        o[u"restartRulesActive"_s] = item.restartRulesActive;

        QJsonObject emailObj;
        emailObj[u"enabled"_s] = item.emailNotify.enabled;
        emailObj[u"onGuardTriggered"_s] = item.emailNotify.onGuardTriggered;
        emailObj[u"onProcessExited"_s] = item.emailNotify.onProcessExited;
        emailObj[u"onRestartFailed"_s] = item.emailNotify.onScheduledRestartFailed;
        emailObj[u"onRetryExhausted"_s] = item.emailNotify.onRetryExhausted;
        emailObj[u"onRunFailed"_s] = item.emailNotify.onScheduledRunFailed;
        emailObj[u"onStartFailed"_s] = item.emailNotify.onStartFailed;
        o[u"emailNotifications"_s] = emailObj;

        QJsonObject statusObj;
        statusObj[u"lastRestart"_s] = item.lastRestart.isValid()
            ? item.lastRestart.toString(Qt::ISODate) : u""_s;
        statusObj[u"restartCount"_s] = item.restartCount;
        statusObj[u"startTime"_s] = item.startTime.isValid()
            ? item.startTime.toString(Qt::ISODate) : u""_s;
        o[u"status"_s] = statusObj;

        itemsArr.append(o);
    }
    root[u"items"_s] = itemsArr;

    QString timestamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    QString defaultName = u"SuperGuardian_Config_%1.json"_s.arg(timestamp);
    QString filePath = QFileDialog::getSaveFileName(this,
        u"导出配置"_s, defaultName, u"JSON Files (*.json)"_s);
    if (filePath.isEmpty())
        return;

    QFile f(filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
        showMessageDialog(this, u"导出配置"_s,
            u"配置已导出到：\n%1"_s.arg(filePath));
        logOperation(u"导出配置到 %1"_s.arg(filePath));
    } else {
        showMessageDialog(this, u"导出失败"_s,
            u"无法写入文件：%1"_s.arg(filePath));
    }
}
