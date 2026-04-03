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
#include <algorithm>

using namespace Qt::Literals::StringLiterals;

static QJsonObject structuredItemToFlat(const QJsonObject& src) {
    QJsonObject o;
    o[u"path"_s] = src[u"path"_s].toString();
    if (src.contains(u"launchArgs"_s))
        o[u"launchArgs"_s] = src[u"launchArgs"_s].toString();
    o[u"note"_s] = src[u"note"_s].toString();
    o[u"pinned"_s] = src[u"pinned"_s].toBool();
    o[u"insertionOrder"_s] = src[u"insertionOrder"_s].toInt();
    o[u"scheduledRunEnabled"_s] = src[u"scheduledRunEnabled"_s].toBool();
    o[u"startDelaySecs"_s] = src.contains(u"startDelaySecs"_s) ? src[u"startDelaySecs"_s].toInt() : 1;
    o[u"trackRunDuration"_s] = src[u"trackRunDuration"_s].toBool();

    if (src.contains(u"guard"_s) && src[u"guard"_s].isObject()) {
        QJsonObject g = src[u"guard"_s].toObject();
        o[u"guard"_s] = g[u"enabled"_s].toBool();
        o[u"guardStartTime"_s] = g[u"startTime"_s].toString();
    } else {
        o[u"guard"_s] = src[u"guard"_s].toBool();
    }

    if (src.contains(u"retry"_s) && src[u"retry"_s].isObject()) {
        QJsonObject r = src[u"retry"_s].toObject();
        o[u"retryIntervalSecs"_s] = r.contains(u"intervalSecs"_s) ? r[u"intervalSecs"_s].toInt() : 30;
        o[u"retryMaxRetries"_s] = r.contains(u"maxRetries"_s) ? r[u"maxRetries"_s].toInt() : 10;
        o[u"retryMaxDurationSecs"_s] = r.contains(u"maxDurationSecs"_s) ? r[u"maxDurationSecs"_s].toInt() : 300;
    } else {
        o[u"retryIntervalSecs"_s] = src.contains(u"retryIntervalSecs"_s) ? src[u"retryIntervalSecs"_s].toInt() : 30;
        o[u"retryMaxRetries"_s] = src.contains(u"retryMaxRetries"_s) ? src[u"retryMaxRetries"_s].toInt() : 10;
        o[u"retryMaxDurationSecs"_s] = src.contains(u"retryMaxDurationSecs"_s) ? src[u"retryMaxDurationSecs"_s].toInt() : 300;
    }

    if (src.contains(u"restartRules"_s) && src[u"restartRules"_s].isArray()) {
        o[u"restartRulesJson"_s] = QString::fromUtf8(
            QJsonDocument(src[u"restartRules"_s].toArray()).toJson(QJsonDocument::Compact));
    } else if (src.contains(u"restartRulesJson"_s)) {
        o[u"restartRulesJson"_s] = src[u"restartRulesJson"_s].toString();
    }
    o[u"restartRulesActive"_s] = src[u"restartRulesActive"_s].toBool();

    if (src.contains(u"runRules"_s) && src[u"runRules"_s].isArray()) {
        o[u"runRulesJson"_s] = QString::fromUtf8(
            QJsonDocument(src[u"runRules"_s].toArray()).toJson(QJsonDocument::Compact));
    } else if (src.contains(u"runRulesJson"_s)) {
        o[u"runRulesJson"_s] = src[u"runRulesJson"_s].toString();
    }

    if (src.contains(u"emailNotifications"_s) && src[u"emailNotifications"_s].isObject()) {
        QJsonObject e = src[u"emailNotifications"_s].toObject();
        o[u"emailNotifyEnabled"_s] = e[u"enabled"_s].toBool();
        o[u"emailOnGuardTriggered"_s] = e[u"onGuardTriggered"_s].toBool();
        o[u"emailOnProcessExited"_s] = e[u"onProcessExited"_s].toBool();
        o[u"emailOnRestartFailed"_s] = e.contains(u"onRestartFailed"_s) ? e[u"onRestartFailed"_s].toBool() : true;
        o[u"emailOnRetryExhausted"_s] = e.contains(u"onRetryExhausted"_s) ? e[u"onRetryExhausted"_s].toBool() : true;
        o[u"emailOnRunFailed"_s] = e.contains(u"onRunFailed"_s) ? e[u"onRunFailed"_s].toBool() : true;
        o[u"emailOnStartFailed"_s] = e.contains(u"onStartFailed"_s) ? e[u"onStartFailed"_s].toBool() : true;
    } else {
        o[u"emailNotifyEnabled"_s] = src[u"emailNotifyEnabled"_s].toBool();
        o[u"emailOnGuardTriggered"_s] = src[u"emailOnGuardTriggered"_s].toBool();
        o[u"emailOnStartFailed"_s] = src.contains(u"emailOnStartFailed"_s) ? src[u"emailOnStartFailed"_s].toBool() : true;
        o[u"emailOnRestartFailed"_s] = src.contains(u"emailOnRestartFailed"_s) ? src[u"emailOnRestartFailed"_s].toBool() : true;
        o[u"emailOnRunFailed"_s] = src.contains(u"emailOnRunFailed"_s) ? src[u"emailOnRunFailed"_s].toBool() : true;
        o[u"emailOnProcessExited"_s] = src[u"emailOnProcessExited"_s].toBool();
        o[u"emailOnRetryExhausted"_s] = src.contains(u"emailOnRetryExhausted"_s) ? src[u"emailOnRetryExhausted"_s].toBool() : true;
    }

    if (src.contains(u"status"_s) && src[u"status"_s].isObject()) {
        QJsonObject st = src[u"status"_s].toObject();
        o[u"lastRestart"_s] = st[u"lastRestart"_s].toString();
        o[u"restartCount"_s] = st[u"restartCount"_s].toInt();
        o[u"startTime"_s] = st[u"startTime"_s].toString();
    } else {
        o[u"lastRestart"_s] = src[u"lastRestart"_s].toString();
        o[u"restartCount"_s] = src[u"restartCount"_s].toInt();
        o[u"startTime"_s] = src[u"startTime"_s].toString();
    }

    return o;
}

static bool tryImportJson(const QString& filePath, QJsonObject& outJson) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return false;

    QJsonObject root = doc.object();
    if (!root.contains(u"items"_s))
        return false;

    QJsonValue itemsVal = root[u"items"_s];
    if (itemsVal.isArray()) {
        QJsonArray srcArr = itemsVal.toArray();
        QJsonArray flatArr;
        for (const QJsonValue& v : srcArr) {
            if (!v.isObject())
                continue;
            QJsonObject srcItem = v.toObject();
            if (srcItem[u"path"_s].toString().isEmpty())
                continue;
            flatArr.append(srcItem.contains(u"guard"_s) && srcItem[u"guard"_s].isObject()
                ? structuredItemToFlat(srcItem)
                : srcItem);
        }

        outJson = QJsonObject();
        outJson[u"items"_s] = QString::fromUtf8(QJsonDocument(flatArr).toJson(QJsonDocument::Compact));

        if (root.contains(u"alwaysOnTop"_s))
            outJson[u"alwaysOnTop"_s] = root[u"alwaysOnTop"_s].toBool();
        if (root.contains(u"emailEnabled"_s))
            outJson[u"emailEnabled"_s] = root[u"emailEnabled"_s].toBool();
        if (root.contains(u"theme"_s))
            outJson[u"theme"_s] = root[u"theme"_s].toString();

        if (root.contains(u"smtp"_s) && root[u"smtp"_s].isObject()) {
            QJsonObject smtp = root[u"smtp"_s].toObject();
            outJson[u"smtp/server"_s] = smtp[u"server"_s].toString();
            outJson[u"smtp/port"_s] = smtp[u"port"_s].toInt(587);
            outJson[u"smtp/useTls"_s] = smtp[u"useTls"_s].toBool(true);
            outJson[u"smtp/username"_s] = smtp[u"username"_s].toString();
            outJson[u"smtp/password"_s] = smtp[u"password"_s].toString();
            outJson[u"smtp/fromAddress"_s] = smtp[u"fromAddress"_s].toString();
            outJson[u"smtp/fromName"_s] = smtp[u"fromName"_s].toString();
            outJson[u"smtp/toAddress"_s] = smtp[u"toAddress"_s].toString();
        }
        return true;
    }

    if (itemsVal.isString()) {
        outJson = root;
        return true;
    }
    return false;
}

static bool tryImportIni(const QString& filePath, QJsonObject& outJson) {
    QSettings s(filePath, QSettings::IniFormat);
    if (s.status() != QSettings::NoError)
        return false;

    int size = s.beginReadArray("items");
    if (size <= 0) {
        s.endArray();
        return false;
    }

    QJsonArray itemsArr;
    for (int i = 0; i < size; ++i) {
        s.setArrayIndex(i);
        if (!s.contains("path") || s.value("path").toString().isEmpty()) {
            s.endArray();
            return false;
        }
        QJsonObject item;
        for (const QString& key : s.childKeys())
            item[key] = QJsonValue::fromVariant(s.value(key));
        itemsArr.append(item);
    }
    s.endArray();

    outJson = QJsonObject();
    outJson[u"items"_s] = QString::fromUtf8(QJsonDocument(itemsArr).toJson(QJsonDocument::Compact));

    s.beginGroup("smtp");
    for (const QString& key : s.childKeys())
        outJson[u"smtp/"_s + key] = QJsonValue::fromVariant(s.value(key));
    s.endGroup();

    const QStringList topKeys = {
        u"emailEnabled"_s, u"duplicateWhitelist"_s, u"theme"_s, u"minimizeToTray"_s,
        u"columnWidths"_s, u"sortSection"_s, u"sortState"_s, u"hiddenColumns"_s, u"headerOrder"_s
    };
    for (const QString& key : topKeys) {
        if (s.contains(key))
            outJson[key] = QJsonValue::fromVariant(s.value(key));
    }
    return true;
}

void SuperGuardian::importConfig() {
    QString filePath = QFileDialog::getOpenFileName(this,
        u"导入配置"_s, QString(), u"配置文件 (*.json *.ini);;JSON Files (*.json);;INI Files (*.ini)"_s);
    if (filePath.isEmpty())
        return;

    QJsonObject json;
    bool ok = tryImportJson(filePath, json);
    if (!ok)
        ok = tryImportIni(filePath, json);
    if (!ok) {
        showMessageDialog(this, u"导入失败"_s,
            u"配置文件格式无效或不包含有效的程序列表。"_s);
        return;
    }

    ConfigDatabase::instance().importFromJson(json);
    items.clear();
    tableWidget->setRowCount(0);
    loadSettings();
    applySavedTrayOptions();

    auto& db = ConfigDatabase::instance();
    QString theme = db.contains(u"theme"_s) ? db.value(u"theme"_s).toString() : detectSystemThemeName();
    applyTheme(theme);

    showMessageDialog(this, u"导入配置"_s, u"配置已成功导入。"_s);
    logOperation(u"导入配置从 %1"_s.arg(filePath));
}

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
