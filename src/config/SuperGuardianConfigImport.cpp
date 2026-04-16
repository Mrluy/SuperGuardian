#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <windows.h>

using namespace Qt::Literals::StringLiterals;

namespace {

int promptImportMode(QWidget* parent, const QString& title) {
    bool ok = false;
    const QString mode = showItemDialog(parent, title, u"请选择导入方式："_s,
        { u"增量导入（保留现有项）"_s, u"覆盖导入（清空后导入）"_s }, &ok);
    if (!ok)
        return 0;
    return mode.startsWith(u"覆盖"_s) ? 2 : 1;
}

QString normalizedPathKey(const QString& path) {
    return QDir::toNativeSeparators(path.trimmed()).toLower();
}

// 将结构化 JSON item（导出格式）转换为 loadSettings 能读取的 flat 格式
QJsonObject structuredItemToFlat(const QJsonObject& src) {
    QJsonObject o;

    // 直通字段
    auto copyIfExists = [&](const QString& key) {
        if (src.contains(key)) o[key] = src[key];
    };
    copyIfExists(u"id"_s);
    copyIfExists(u"path"_s);
    copyIfExists(u"launchArgs"_s);
    copyIfExists(u"note"_s);
    copyIfExists(u"pinned"_s);
    copyIfExists(u"insertionOrder"_s);
    copyIfExists(u"scheduledRunEnabled"_s);
    copyIfExists(u"startDelaySecs"_s);
    copyIfExists(u"trackRunDuration"_s);
    copyIfExists(u"runHideWindow"_s);
    copyIfExists(u"lastRunHidden"_s);
    copyIfExists(u"restartRulesActive"_s);

    // guard: {enabled, startTime} → guard(bool), guardStartTime(string)
    if (src.contains(u"guard"_s) && src[u"guard"_s].isObject()) {
        QJsonObject g = src[u"guard"_s].toObject();
        o[u"guard"_s] = g[u"enabled"_s].toBool();
        o[u"guardStartTime"_s] = g[u"startTime"_s].toString();
    } else if (src.contains(u"guard"_s)) {
        // 兼容旧版 flat 格式（guard 直接是 bool）
        o[u"guard"_s] = src[u"guard"_s];
        copyIfExists(u"guardStartTime"_s);
    }

    // retry: {intervalSecs, maxDurationSecs, maxRetries} → flat
    if (src.contains(u"retry"_s) && src[u"retry"_s].isObject()) {
        QJsonObject r = src[u"retry"_s].toObject();
        o[u"retryIntervalSecs"_s] = r[u"intervalSecs"_s].toInt(30);
        o[u"retryMaxDurationSecs"_s] = r[u"maxDurationSecs"_s].toInt(300);
        o[u"retryMaxRetries"_s] = r[u"maxRetries"_s].toInt(10);
    } else {
        copyIfExists(u"retryIntervalSecs"_s);
        copyIfExists(u"retryMaxDurationSecs"_s);
        copyIfExists(u"retryMaxRetries"_s);
    }

    // emailNotifications: {enabled, ...} → flat
    if (src.contains(u"emailNotifications"_s) && src[u"emailNotifications"_s].isObject()) {
        QJsonObject e = src[u"emailNotifications"_s].toObject();
        o[u"emailNotifyEnabled"_s] = e[u"enabled"_s].toBool();
        o[u"emailOnGuardTriggered"_s] = e[u"onGuardTriggered"_s].toBool();
        o[u"emailOnStartFailed"_s] = e.contains(u"onStartFailed"_s) ? e[u"onStartFailed"_s].toBool() : true;
        o[u"emailOnRestartFailed"_s] = e.contains(u"onRestartFailed"_s) ? e[u"onRestartFailed"_s].toBool() : true;
        o[u"emailOnRunFailed"_s] = e.contains(u"onRunFailed"_s) ? e[u"onRunFailed"_s].toBool() : true;
        o[u"emailOnProcessExited"_s] = e[u"onProcessExited"_s].toBool();
        o[u"emailOnRetryExhausted"_s] = e.contains(u"onRetryExhausted"_s) ? e[u"onRetryExhausted"_s].toBool() : true;
    } else {
        copyIfExists(u"emailNotifyEnabled"_s);
        copyIfExists(u"emailOnGuardTriggered"_s);
        copyIfExists(u"emailOnStartFailed"_s);
        copyIfExists(u"emailOnRestartFailed"_s);
        copyIfExists(u"emailOnRunFailed"_s);
        copyIfExists(u"emailOnProcessExited"_s);
        copyIfExists(u"emailOnRetryExhausted"_s);
    }

    // status: {lastRestart, restartCount, startTime} → flat
    if (src.contains(u"status"_s) && src[u"status"_s].isObject()) {
        QJsonObject s = src[u"status"_s].toObject();
        o[u"lastRestart"_s] = s[u"lastRestart"_s].toString();
        o[u"restartCount"_s] = s[u"restartCount"_s].toInt();
        o[u"startTime"_s] = s[u"startTime"_s].toString();
    } else {
        copyIfExists(u"lastRestart"_s);
        copyIfExists(u"restartCount"_s);
        copyIfExists(u"startTime"_s);
    }

    // runRules: QJsonArray → runRulesJson (compact JSON string)
    if (src.contains(u"runRules"_s) && src[u"runRules"_s].isArray()) {
        o[u"runRulesJson"_s] = QString::fromUtf8(
            QJsonDocument(src[u"runRules"_s].toArray()).toJson(QJsonDocument::Compact));
    } else {
        copyIfExists(u"runRulesJson"_s);
    }

    // restartRules: QJsonArray → restartRulesJson (compact JSON string)
    if (src.contains(u"restartRules"_s) && src[u"restartRules"_s].isArray()) {
        o[u"restartRulesJson"_s] = QString::fromUtf8(
            QJsonDocument(src[u"restartRules"_s].toArray()).toJson(QJsonDocument::Compact));
    } else {
        copyIfExists(u"restartRulesJson"_s);
    }

    return o;
}

// 将结构化 JSON 转换为 db.importFromJson 可用的 flat QJsonObject
QJsonObject structuredJsonToFlat(const QJsonObject& root) {
    QJsonObject flat;

    // 简单顶层字段
    if (root.contains(u"alwaysOnTop"_s)) flat[u"alwaysOnTop"_s] = root[u"alwaysOnTop"_s];
    if (root.contains(u"autoCheckUpdates"_s)) flat[u"autoCheckUpdates"_s] = root[u"autoCheckUpdates"_s];
    if (root.contains(u"emailEnabled"_s)) flat[u"emailEnabled"_s] = root[u"emailEnabled"_s];
    if (root.contains(u"theme"_s)) flat[u"theme"_s] = root[u"theme"_s];

    // smtp: 嵌套对象 → smtp/server, smtp/port, ...
    if (root.contains(u"smtp"_s) && root[u"smtp"_s].isObject()) {
        QJsonObject smtp = root[u"smtp"_s].toObject();
        if (smtp.contains(u"server"_s)) flat[u"smtp/server"_s] = smtp[u"server"_s];
        if (smtp.contains(u"port"_s)) flat[u"smtp/port"_s] = smtp[u"port"_s];
        if (smtp.contains(u"useTls"_s)) flat[u"smtp/useTls"_s] = smtp[u"useTls"_s];
        if (smtp.contains(u"username"_s)) flat[u"smtp/username"_s] = smtp[u"username"_s];
        if (smtp.contains(u"password"_s)) flat[u"smtp/password"_s] = smtp[u"password"_s];
        if (smtp.contains(u"fromAddress"_s)) flat[u"smtp/fromAddress"_s] = smtp[u"fromAddress"_s];
        if (smtp.contains(u"fromName"_s)) flat[u"smtp/fromName"_s] = smtp[u"fromName"_s];
        if (smtp.contains(u"toAddress"_s)) flat[u"smtp/toAddress"_s] = smtp[u"toAddress"_s];
    }

    // items: 结构化数组 → flat items JSON string
    QJsonArray itemsArr;
    if (root.contains(u"items"_s)) {
        QJsonArray srcItems;
        if (root[u"items"_s].isArray()) {
            srcItems = root[u"items"_s].toArray();
        } else if (root[u"items"_s].isString()) {
            QJsonDocument d = QJsonDocument::fromJson(root[u"items"_s].toString().toUtf8());
            if (d.isArray()) srcItems = d.array();
        }
        for (const QJsonValue& v : srcItems)
            itemsArr.append(structuredItemToFlat(v.toObject()));
    }
    flat[u"items"_s] = QString::fromUtf8(QJsonDocument(itemsArr).toJson(QJsonDocument::Compact));

    return flat;
}

QJsonArray parseFlatItemsArray(const QJsonValue& value) {
    if (value.isString()) {
        QJsonDocument doc = QJsonDocument::fromJson(value.toString().toUtf8());
        if (doc.isArray()) return doc.array();
    }
    if (value.isArray()) return value.toArray();
    return {};
}

}

void SuperGuardian::importConfig() {
    QString filePath = QFileDialog::getOpenFileName(this,
        u"导入配置"_s, QString(), u"JSON Files (*.json)"_s);
    if (filePath.isEmpty())
        return;

    const int importMode = promptImportMode(this, u"导入配置"_s);
    if (importMode == 0)
        return;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showMessageDialog(this, u"导入失败"_s,
            u"无法读取文件：%1"_s.arg(filePath));
        return;
    }

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (!doc.isObject()) {
        showMessageDialog(this, u"导入失败"_s,
            err.error == QJsonParseError::NoError
                ? u"JSON 文件格式无效。"_s
                : u"JSON 解析失败：%1"_s.arg(err.errorString()));
        return;
    }

    // 将结构化 JSON 转换为 flat 格式
    QJsonObject importedFlat = structuredJsonToFlat(doc.object());
    auto& db = ConfigDatabase::instance();
    QJsonObject finalJson;

    if (importMode == 1) {
        // 增量导入：合并
        QJsonObject currentJson = db.exportToJson();
        finalJson = currentJson;

        // 覆盖非 items 字段
        for (auto it = importedFlat.constBegin(); it != importedFlat.constEnd(); ++it) {
            if (it.key() != u"items"_s)
                finalJson[it.key()] = it.value();
        }

        // 合并 items
        QJsonArray currentItems = parseFlatItemsArray(currentJson.value(u"items"_s));
        QJsonArray importedItems = parseFlatItemsArray(importedFlat.value(u"items"_s));

        QHash<QString, int> pathIndex;
        for (int i = 0; i < currentItems.size(); ++i) {
            QJsonObject item = currentItems[i].toObject();
            QString key = normalizedPathKey(item.value(u"path"_s).toString());
            if (!key.isEmpty())
                pathIndex[key] = i;
        }

        for (const QJsonValue& value : importedItems) {
            QJsonObject item = value.toObject();
            QString key = normalizedPathKey(item.value(u"path"_s).toString());
            if (key.isEmpty())
                continue;
            if (pathIndex.contains(key)) {
                currentItems[pathIndex[key]] = item;
            } else {
                pathIndex[key] = currentItems.size();
                currentItems.append(item);
            }
        }

        for (int i = 0; i < currentItems.size(); ++i) {
            QJsonObject item = currentItems[i].toObject();
            item[u"insertionOrder"_s] = i;
            currentItems[i] = item;
        }

        finalJson[u"items"_s] = QString::fromUtf8(
            QJsonDocument(currentItems).toJson(QJsonDocument::Compact));
    } else {
        // 覆盖导入
        finalJson = importedFlat;
        QJsonArray importedItems = parseFlatItemsArray(finalJson.value(u"items"_s));
        for (int i = 0; i < importedItems.size(); ++i) {
            QJsonObject item = importedItems[i].toObject();
            item[u"insertionOrder"_s] = i;
            importedItems[i] = item;
        }
        finalJson[u"items"_s] = QString::fromUtf8(
            QJsonDocument(importedItems).toJson(QJsonDocument::Compact));
    }

    db.importFromJson(finalJson);
    items.clear();
    tableWidget->setRowCount(0);
    loadSettings();
    applySavedTrayOptions();

    db.setValue(u"globalGuardEnabled"_s, false);
    db.setValue(u"globalRestartEnabled"_s, false);
    db.setValue(u"globalRunEnabled"_s, false);
    if (globalGuardAct) { globalGuardAct->blockSignals(true); globalGuardAct->setChecked(false); globalGuardAct->blockSignals(false); }
    if (globalRestartAct) { globalRestartAct->blockSignals(true); globalRestartAct->setChecked(false); globalRestartAct->blockSignals(false); }
    if (globalRunAct) { globalRunAct->blockSignals(true); globalRunAct->setChecked(false); globalRunAct->blockSignals(false); }
    updateToolbarIcons();

    QString theme = db.contains(u"theme"_s) ? db.value(u"theme"_s).toString() : detectSystemThemeName();
    applyTheme(theme);

    showMessageDialog(this, u"导入配置"_s,
        importMode == 1
            ? u"配置已成功增量导入。\n\n注意：所有全局功能（全局守护、全局定时重启、全局定时运行）已自动关闭，请根据需要手动开启。"_s
            : u"配置已成功覆盖导入。\n\n注意：所有全局功能（全局守护、全局定时重启、全局定时运行）已自动关闭，请根据需要手动开启。"_s);
    logOperation((importMode == 1 ? u"增量导入配置从 %1"_s : u"覆盖导入配置从 %1"_s).arg(filePath));
}

// ---- 导出配置 ----

static QJsonArray exportScheduleRules(const QList<ScheduleRule>& rules) {
    QJsonArray arr;
    for (const ScheduleRule& r : rules) {
        QJsonObject o;
        if (r.type == ScheduleRule::Advanced)
            o[u"type"_s] = u"advanced"_s;
        else
            o[u"type"_s] = (r.type == ScheduleRule::Periodic) ? u"periodic"_s : u"fixed"_s;
        o[u"intervalSecs"_s] = r.intervalSecs;
        o[u"fixedTime"_s] = r.fixedTime.isValid() ? r.fixedTime.toString(u"HH:mm:ss"_s) : u""_s;
        QJsonArray days;
        for (int d : r.daysOfWeek) days.append(d);
        o[u"daysOfWeek"_s] = days;
        o[u"nextTrigger"_s] = r.nextTrigger.isValid() ? r.nextTrigger.toString(Qt::ISODate) : u""_s;
        if (r.type == ScheduleRule::Advanced) {
            o[u"advSecond"_s] = r.advSecond;
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
    root[u"autoCheckUpdates"_s] = db.value(u"autoCheckUpdates"_s, false).toBool();
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
        o[u"runHideWindow"_s] = item.runHideWindow;
        o[u"lastRunHidden"_s] = item.lastRunHidden;
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

// ---- 重置配置 ----

void SuperGuardian::resetConfig() {
    if (!showMessageDialog(this, u"重置配置"_s,
        u"确认重置全部配置吗？此操作将清除所有设置和程序列表。"_s, true))
        return;

    ConfigDatabase::instance().importFromJson(QJsonObject());
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
    ConfigDatabase::instance().setValue(u"theme"_s, theme);
    applyTheme(theme);

    showMessageDialog(this, u"重置配置"_s, u"配置已重置为默认设置。"_s);
    logOperation(u"重置全部配置"_s);
}

// ---- 重建表格 ----

void SuperGuardian::rebuildTableFromItems() {
    tableWidget->setRowCount(0);
    auto appendRows = [this](bool pinned) {
        for (const GuardItem& item : items) {
            if (item.pinned != pinned)
                continue;
            int row = tableWidget->rowCount();
            tableWidget->insertRow(row);
            setupTableRow(row, item);
        }
    };
    appendRows(true);
    appendRows(false);
}

// ---- 导出诊断信息 ----

void SuperGuardian::exportDiagnosticInfo() {
    QString timestamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    QString defaultName = u"SuperGuardian_Diagnostic_%1.txt"_s.arg(timestamp);
    QString filePath = QFileDialog::getSaveFileName(this,
        u"导出诊断信息"_s, defaultName, u"文本文件 (*.txt)"_s);
    if (filePath.isEmpty())
        return;

    QStringList lines;
    auto section = [&](const QString& title) {
        lines << QString();
        lines << u"═══════════════════════════════════════════════════════"_s;
        lines << u"  %1"_s.arg(title);
        lines << u"═══════════════════════════════════════════════════════"_s;
    };

    section(u"系统与应用信息"_s);
    lines << u"导出时间: %1"_s.arg(QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s));
    lines << u"应用版本: %1"_s.arg(QCoreApplication::applicationVersion());
    lines << u"Qt 版本: %1"_s.arg(qVersion());
    lines << u"操作系统: %1"_s.arg(QSysInfo::prettyProductName());
    lines << u"CPU 架构: %1"_s.arg(QSysInfo::currentCpuArchitecture());
    lines << u"应用路径: %1"_s.arg(QCoreApplication::applicationFilePath());
    lines << u"数据目录: %1"_s.arg(appDataDirPath());
    lines << u"PID: %1"_s.arg(QCoreApplication::applicationPid());

    {
        QByteArray probe = u"中文"_s.toUtf8();
        QString hex;
        for (int i = 0; i < probe.size(); ++i) {
            if (i)
                hex += u' ';
            hex += QString::asprintf("%02X", static_cast<unsigned char>(probe[i]));
        }
        lines << u"编码验证: \"%1\" → [%2] (预期: E4 B8 AD E6 96 87)"_s.arg(u"中文"_s, hex);
    }

    section(u"当前配置"_s);
    auto& db = ConfigDatabase::instance();
    QJsonObject allConfig = db.exportToJson();
    for (auto it = allConfig.begin(); it != allConfig.end(); ++it) {
        QString val = it.value().isString()
            ? it.value().toString()
            : QString::fromUtf8(QJsonDocument(QJsonArray{ it.value() }).toJson(QJsonDocument::Compact));
        if (val.length() > 200)
            val = val.left(200) + u"... (已截断)"_s;
        lines << u"  %1 = %2"_s.arg(it.key(), val);
    }

    section(u"守护项状态 (共 %1 项)"_s.arg(items.size()));
    for (int i = 0; i < items.size(); ++i) {
        const GuardItem& item = items[i];
        lines << u"--- [%1] %2 ---"_s.arg(i).arg(item.path);
        lines << u"  进程名: %1"_s.arg(item.processName);
        lines << u"  目标路径: %1"_s.arg(item.targetPath);
        if (!item.launchArgs.isEmpty())
            lines << u"  启动参数: %1"_s.arg(item.launchArgs);
        if (!item.note.isEmpty())
            lines << u"  备注: %1"_s.arg(item.note);
        lines << u"  守护中: %1"_s.arg(item.guarding ? u"是"_s : u"否"_s);
        lines << u"  已置顶: %1"_s.arg(item.pinned ? u"是"_s : u"否"_s);
        lines << u"  被守护次数: %1"_s.arg(item.restartCount);
        if (item.startTime.isValid())
            lines << u"  启动时间: %1"_s.arg(item.startTime.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        if (item.guardStartTime.isValid())
            lines << u"  守护开始: %1"_s.arg(item.guardStartTime.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        if (item.lastRestart.isValid())
            lines << u"  上次重启: %1"_s.arg(item.lastRestart.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        lines << u"  定时重启: %1 (%2条规则)"_s.arg(item.restartRulesActive ? u"启用"_s : u"停用"_s).arg(item.restartRules.size());
        lines << u"  定时运行: %1 (%2条规则)"_s.arg(item.scheduledRunEnabled ? u"启用"_s : u"停用"_s).arg(item.runRules.size());
        lines << u"  启动延时: %1秒"_s.arg(item.startDelaySecs);
        lines << u"  重试配置: 间隔%1秒, 最多%2次, 最长%3秒"_s
            .arg(item.retryConfig.retryIntervalSecs)
            .arg(item.retryConfig.maxRetries)
            .arg(item.retryConfig.maxDurationSecs);
        if (item.retryActive) {
            lines << u"  重试中: 当前第%1次, 开始于 %2"_s
                .arg(item.currentRetryCount)
                .arg(item.retryStartTime.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        }
        int procCount = 0;
        bool running = isProcessRunning(item.processName, procCount);
        lines << u"  当前进程状态: %1 (实例数: %2)"_s.arg(running ? u"运行中"_s : u"未运行"_s).arg(procCount);
    }

    section(u"自我守护状态"_s);
    bool selfGuardEnabled = db.value(u"self_guard_enabled"_s, false).toBool();
    bool manualExit = db.value(u"self_guard_manual_exit"_s, false).toBool();
    int watchdogPid = db.value(u"watchdog_pid"_s, 0).toInt();
    lines << u"  自我守护: %1"_s.arg(selfGuardEnabled ? u"启用"_s : u"停用"_s);
    lines << u"  手动退出标记: %1"_s.arg(manualExit ? u"是"_s : u"否"_s);
    lines << u"  看门狗 PID: %1"_s.arg(watchdogPid > 0 ? QString::number(watchdogPid) : u"无"_s);
    if (watchdogPid > 0) {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>(watchdogPid));
        lines << u"  看门狗进程存活: %1"_s.arg(h ? u"是"_s : u"否"_s);
        if (h)
            CloseHandle(h);
    }

    auto appendLogs = [&](const QString& category, const QString& title, int limit) {
        section(u"最近%1 (最多%2条)"_s.arg(title).arg(limit));
        auto logs = LogDatabase::instance().queryLogs(category, limit);
        if (logs.isEmpty()) {
            lines << u"  (无记录)"_s;
            return;
        }
        for (const LogEntry& entry : logs) {
            QString program = entry.program.isEmpty() ? QString() : u" [%1]"_s.arg(entry.program);
            lines << u"  %1%2 %3"_s.arg(entry.timestamp.toString(u"MM-dd HH:mm:ss"_s), program, entry.message);
        }
    };
    appendLogs(u"runtime"_s, u"运行日志"_s, 50);
    appendLogs(u"operation"_s, u"操作日志"_s, 30);
    appendLogs(u"guard"_s, u"守护日志"_s, 30);
    appendLogs(u"scheduled_restart"_s, u"定时重启日志"_s, 20);
    appendLogs(u"scheduled_run"_s, u"定时运行日志"_s, 20);

    QFile f(filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("\xEF\xBB\xBF");
        f.write(lines.join(u"\r\n"_s).toUtf8());
        f.close();
        showMessageDialog(this, u"导出诊断信息"_s,
            u"诊断信息已导出到：\n%1"_s.arg(filePath));
        logOperation(u"导出诊断信息到 %1"_s.arg(filePath));
    } else {
        showMessageDialog(this, u"导出失败"_s,
            u"无法写入文件：%1"_s.arg(filePath));
    }
}
