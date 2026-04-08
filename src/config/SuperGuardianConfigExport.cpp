#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::Literals::StringLiterals;

static QString csvEscape(const QString& s) {
    if (s.contains(u',') || s.contains(u'"') || s.contains(u'\n'))
        return u"\"%1\""_s.arg(QString(s).replace(u"\""_s, u"\"\""_s));
    return s;
}

static QJsonArray scheduleRulesToExportJson(const QList<ScheduleRule>& rules) {
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
        for (int d : r.daysOfWeek) days.append(d);
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

// 固定 CSV 列顺序
static const QStringList csvHeaders = {
    u"path"_s, u"launchArgs"_s, u"note"_s, u"pinned"_s, u"insertionOrder"_s,
    u"guarding"_s, u"guardStartTime"_s,
    u"scheduledRunEnabled"_s, u"trackRunDuration"_s, u"runHideWindow"_s, u"lastRunHidden"_s,
    u"startDelaySecs"_s, u"restartRulesActive"_s,
    u"runRulesJson"_s, u"restartRulesJson"_s,
    u"retryIntervalSecs"_s, u"retryMaxRetries"_s, u"retryMaxDurationSecs"_s,
    u"emailNotifyEnabled"_s, u"emailOnGuardTriggered"_s, u"emailOnStartFailed"_s,
    u"emailOnRestartFailed"_s, u"emailOnRunFailed"_s, u"emailOnProcessExited"_s,
    u"emailOnRetryExhausted"_s,
    u"lastRestart"_s, u"restartCount"_s, u"startTime"_s
};

void SuperGuardian::exportConfig() {
    saveSettings();
    auto& db = ConfigDatabase::instance();

    QString timestamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    QString defaultName = u"SuperGuardian_Config_%1.csv"_s.arg(timestamp);
    QString filePath = QFileDialog::getSaveFileName(this,
        u"导出配置"_s, defaultName, u"CSV Files (*.csv)"_s);
    if (filePath.isEmpty())
        return;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        showMessageDialog(this, u"导出失败"_s,
            u"无法写入文件：%1"_s.arg(filePath));
        return;
    }

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << QChar(0xFEFF); // UTF-8 BOM

    // 全局设置（注释行）
    out << u"# alwaysOnTop="_s << (db.value(u"alwaysOnTop"_s, false).toBool() ? u"true"_s : u"false"_s) << u"\n"_s;
    out << u"# emailEnabled="_s << (db.value(u"emailEnabled"_s, false).toBool() ? u"true"_s : u"false"_s) << u"\n"_s;
    out << u"# theme="_s << db.value(u"theme"_s, u"dark"_s).toString() << u"\n"_s;
    out << u"# smtp.server="_s << smtpConfig.server << u"\n"_s;
    out << u"# smtp.port="_s << smtpConfig.port << u"\n"_s;
    out << u"# smtp.useTls="_s << (smtpConfig.useTls ? u"true"_s : u"false"_s) << u"\n"_s;
    out << u"# smtp.username="_s << smtpConfig.username << u"\n"_s;
    out << u"# smtp.password="_s << smtpConfig.password << u"\n"_s;
    out << u"# smtp.fromAddress="_s << smtpConfig.fromAddress << u"\n"_s;
    out << u"# smtp.fromName="_s << smtpConfig.fromName << u"\n"_s;
    out << u"# smtp.toAddress="_s << smtpConfig.toAddress << u"\n"_s;

    // CSV 表头
    out << csvHeaders.join(u","_s) << u"\n"_s;

    // 数据行
    for (const GuardItem& item : items) {
        QString runRulesJson = QString::fromUtf8(
            QJsonDocument(scheduleRulesToExportJson(item.runRules)).toJson(QJsonDocument::Compact));
        QString restartRulesJson = QString::fromUtf8(
            QJsonDocument(scheduleRulesToExportJson(item.restartRules)).toJson(QJsonDocument::Compact));

        QStringList cols;
        cols << csvEscape(item.path);
        cols << csvEscape(item.launchArgs);
        cols << csvEscape(item.note);
        cols << (item.pinned ? u"true"_s : u"false"_s);
        cols << QString::number(item.insertionOrder);
        cols << (item.guarding ? u"true"_s : u"false"_s);
        cols << (item.guardStartTime.isValid() ? item.guardStartTime.toString(Qt::ISODate) : u""_s);
        cols << (item.scheduledRunEnabled ? u"true"_s : u"false"_s);
        cols << (item.trackRunDuration ? u"true"_s : u"false"_s);
        cols << (item.runHideWindow ? u"true"_s : u"false"_s);
        cols << (item.lastRunHidden ? u"true"_s : u"false"_s);
        cols << QString::number(item.startDelaySecs);
        cols << (item.restartRulesActive ? u"true"_s : u"false"_s);
        cols << csvEscape(runRulesJson);
        cols << csvEscape(restartRulesJson);
        cols << QString::number(item.retryConfig.retryIntervalSecs);
        cols << QString::number(item.retryConfig.maxRetries);
        cols << QString::number(item.retryConfig.maxDurationSecs);
        cols << (item.emailNotify.enabled ? u"true"_s : u"false"_s);
        cols << (item.emailNotify.onGuardTriggered ? u"true"_s : u"false"_s);
        cols << (item.emailNotify.onStartFailed ? u"true"_s : u"false"_s);
        cols << (item.emailNotify.onScheduledRestartFailed ? u"true"_s : u"false"_s);
        cols << (item.emailNotify.onScheduledRunFailed ? u"true"_s : u"false"_s);
        cols << (item.emailNotify.onProcessExited ? u"true"_s : u"false"_s);
        cols << (item.emailNotify.onRetryExhausted ? u"true"_s : u"false"_s);
        cols << (item.lastRestart.isValid() ? item.lastRestart.toString(Qt::ISODate) : u""_s);
        cols << QString::number(item.restartCount);
        cols << (item.startTime.isValid() ? item.startTime.toString(Qt::ISODate) : u""_s);

        out << cols.join(u","_s) << u"\n"_s;
    }

    f.close();
    showMessageDialog(this, u"导出配置"_s,
        u"配置已导出到：\n%1"_s.arg(filePath));
    logOperation(u"导出配置到 %1"_s.arg(filePath));
}
