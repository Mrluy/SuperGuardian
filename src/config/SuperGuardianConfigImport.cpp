#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include "ThemeManager.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

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
