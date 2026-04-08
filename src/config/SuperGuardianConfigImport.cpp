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

using namespace Qt::Literals::StringLiterals;

static QStringList csvParseLine(const QString& line) {
    QStringList fields;
    QString field;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        QChar c = line[i];
        if (inQuotes) {
            if (c == u'"') {
                if (i + 1 < line.size() && line[i + 1] == u'"') {
                    field += u'"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == u'"') {
                inQuotes = true;
            } else if (c == u',') {
                fields << field;
                field.clear();
            } else {
                field += c;
            }
        }
    }
    fields << field;
    return fields;
}

static bool parseBool(const QString& s) {
    return s.trimmed().compare(u"true"_s, Qt::CaseInsensitive) == 0
        || s.trimmed() == u"1"_s;
}

static int promptImportMode(QWidget* parent, const QString& title) {
    bool ok = false;
    const QString mode = showItemDialog(parent, title, u"请选择导入方式："_s,
        { u"增量导入（保留现有项）"_s, u"覆盖导入（清空后导入）"_s }, &ok);
    if (!ok)
        return 0;
    return mode.startsWith(u"覆盖"_s) ? 2 : 1;
}

void SuperGuardian::importConfig() {
    QString filePath = QFileDialog::getOpenFileName(this,
        u"导入配置"_s, QString(), u"CSV Files (*.csv)"_s);
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

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    // 解析注释行（全局设置）和 CSV 数据
    QMap<QString, QString> globalSettings;
    QStringList headers;
    QList<QStringList> dataRows;

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty()) continue;
        // 跳过 UTF-8 BOM
        if (!globalSettings.isEmpty() || headers.isEmpty()) {
            if (line.startsWith(QChar(0xFEFF)))
                line = line.mid(1);
        }
        if (line.startsWith(u"#"_s)) {
            // 注释行：# key=value
            QString content = line.mid(1).trimmed();
            int eq = content.indexOf(u'=');
            if (eq > 0)
                globalSettings[content.left(eq).trimmed()] = content.mid(eq + 1);
            continue;
        }
        if (headers.isEmpty()) {
            headers = csvParseLine(line);
            continue;
        }
        QStringList row = csvParseLine(line);
        if (!row.isEmpty() && !row[0].trimmed().isEmpty())
            dataRows << row;
    }
    f.close();

    if (headers.isEmpty() || dataRows.isEmpty()) {
        showMessageDialog(this, u"导入失败"_s,
            u"CSV 文件格式无效或不包含有效的程序列表。"_s);
        return;
    }

    // 建立列名到索引的映射
    QMap<QString, int> colIdx;
    for (int i = 0; i < headers.size(); ++i)
        colIdx[headers[i].trimmed()] = i;

    auto getField = [&](const QStringList& row, const QString& name) -> QString {
        int idx = colIdx.value(name, -1);
        if (idx < 0 || idx >= row.size()) return QString();
        return row[idx].trimmed();
    };

    // 应用全局设置
    auto& db = ConfigDatabase::instance();
    if (globalSettings.contains(u"alwaysOnTop"_s))
        db.setValue(u"alwaysOnTop"_s, parseBool(globalSettings[u"alwaysOnTop"_s]));
    if (globalSettings.contains(u"emailEnabled"_s))
        db.setValue(u"emailEnabled"_s, parseBool(globalSettings[u"emailEnabled"_s]));
    if (globalSettings.contains(u"theme"_s))
        db.setValue(u"theme"_s, globalSettings[u"theme"_s]);

    if (globalSettings.contains(u"smtp.server"_s)) {
        smtpConfig.server = globalSettings[u"smtp.server"_s];
        smtpConfig.port = globalSettings.value(u"smtp.port"_s, u"587"_s).toInt();
        smtpConfig.useTls = parseBool(globalSettings.value(u"smtp.useTls"_s, u"true"_s));
        smtpConfig.username = globalSettings.value(u"smtp.username"_s);
        smtpConfig.password = globalSettings.value(u"smtp.password"_s);
        smtpConfig.fromAddress = globalSettings.value(u"smtp.fromAddress"_s);
        smtpConfig.fromName = globalSettings.value(u"smtp.fromName"_s);
        smtpConfig.toAddress = globalSettings.value(u"smtp.toAddress"_s);
        db.setValue(u"smtp/server"_s, smtpConfig.server);
        db.setValue(u"smtp/port"_s, smtpConfig.port);
        db.setValue(u"smtp/useTls"_s, smtpConfig.useTls);
        db.setValue(u"smtp/username"_s, smtpConfig.username);
        db.setValue(u"smtp/password"_s, smtpConfig.password);
        db.setValue(u"smtp/fromAddress"_s, smtpConfig.fromAddress);
        db.setValue(u"smtp/fromName"_s, smtpConfig.fromName);
        db.setValue(u"smtp/toAddress"_s, smtpConfig.toAddress);
    }

    // 构建导入的 items JSON（复用 loadSettings 的格式）
    QJsonArray importedItems;
    for (const QStringList& row : dataRows) {
        QJsonObject o;
        QString path = getField(row, u"path"_s);
        if (path.isEmpty()) continue;
        o[u"path"_s] = path;
        o[u"launchArgs"_s] = getField(row, u"launchArgs"_s);
        o[u"note"_s] = getField(row, u"note"_s);
        o[u"pinned"_s] = parseBool(getField(row, u"pinned"_s));
        o[u"insertionOrder"_s] = getField(row, u"insertionOrder"_s).toInt();
        o[u"guard"_s] = parseBool(getField(row, u"guarding"_s));
        o[u"guardStartTime"_s] = getField(row, u"guardStartTime"_s);
        o[u"scheduledRunEnabled"_s] = parseBool(getField(row, u"scheduledRunEnabled"_s));
        o[u"trackRunDuration"_s] = parseBool(getField(row, u"trackRunDuration"_s));
        o[u"runHideWindow"_s] = parseBool(getField(row, u"runHideWindow"_s));
        o[u"lastRunHidden"_s] = parseBool(getField(row, u"lastRunHidden"_s));
        o[u"startDelaySecs"_s] = qMax(0, getField(row, u"startDelaySecs"_s).toInt());
        o[u"restartRulesActive"_s] = parseBool(getField(row, u"restartRulesActive"_s));

        QString runJson = getField(row, u"runRulesJson"_s);
        if (!runJson.isEmpty()) o[u"runRulesJson"_s] = runJson;
        QString restartJson = getField(row, u"restartRulesJson"_s);
        if (!restartJson.isEmpty()) o[u"restartRulesJson"_s] = restartJson;

        o[u"retryIntervalSecs"_s] = qMax(1, getField(row, u"retryIntervalSecs"_s).toInt());
        o[u"retryMaxRetries"_s] = getField(row, u"retryMaxRetries"_s).toInt();
        o[u"retryMaxDurationSecs"_s] = getField(row, u"retryMaxDurationSecs"_s).toInt();

        o[u"emailNotifyEnabled"_s] = parseBool(getField(row, u"emailNotifyEnabled"_s));
        o[u"emailOnGuardTriggered"_s] = parseBool(getField(row, u"emailOnGuardTriggered"_s));
        o[u"emailOnStartFailed"_s] = parseBool(getField(row, u"emailOnStartFailed"_s));
        o[u"emailOnRestartFailed"_s] = parseBool(getField(row, u"emailOnRestartFailed"_s));
        o[u"emailOnRunFailed"_s] = parseBool(getField(row, u"emailOnRunFailed"_s));
        o[u"emailOnProcessExited"_s] = parseBool(getField(row, u"emailOnProcessExited"_s));
        o[u"emailOnRetryExhausted"_s] = parseBool(getField(row, u"emailOnRetryExhausted"_s));

        o[u"lastRestart"_s] = getField(row, u"lastRestart"_s);
        o[u"restartCount"_s] = getField(row, u"restartCount"_s).toInt();
        o[u"startTime"_s] = getField(row, u"startTime"_s);

        importedItems.append(o);
    }

    // 合并或覆盖
    QJsonObject finalJson;
    if (importMode == 1) {
        // 增量：合并到现有配置
        QJsonObject currentJson = db.exportToJson();
        QJsonArray currentItems;
        QString currentItemsStr = currentJson.value(u"items"_s).toString();
        if (!currentItemsStr.isEmpty()) {
            QJsonDocument cd = QJsonDocument::fromJson(currentItemsStr.toUtf8());
            if (cd.isArray()) currentItems = cd.array();
        }

        // 按 path 去重合并
        QHash<QString, int> pathIndex;
        for (int i = 0; i < currentItems.size(); ++i) {
            QJsonObject ci = currentItems[i].toObject();
            QString key = QDir::toNativeSeparators(ci.value(u"path"_s).toString().trimmed()).toLower();
            if (!key.isEmpty()) pathIndex[key] = i;
        }
        for (const QJsonValue& v : importedItems) {
            QJsonObject item = v.toObject();
            QString key = QDir::toNativeSeparators(item.value(u"path"_s).toString().trimmed()).toLower();
            if (key.isEmpty()) continue;
            if (pathIndex.contains(key))
                currentItems[pathIndex[key]] = item;
            else {
                pathIndex[key] = currentItems.size();
                currentItems.append(item);
            }
        }

        // 重新编号 insertionOrder
        for (int i = 0; i < currentItems.size(); ++i) {
            QJsonObject item = currentItems[i].toObject();
            item[u"insertionOrder"_s] = i;
            currentItems[i] = item;
        }

        finalJson = currentJson;
        finalJson[u"items"_s] = QString::fromUtf8(QJsonDocument(currentItems).toJson(QJsonDocument::Compact));
    } else {
        // 覆盖：直接使用导入的
        for (int i = 0; i < importedItems.size(); ++i) {
            QJsonObject item = importedItems[i].toObject();
            item[u"insertionOrder"_s] = i;
            importedItems[i] = item;
        }
        finalJson = db.exportToJson();
        finalJson[u"items"_s] = QString::fromUtf8(QJsonDocument(importedItems).toJson(QJsonDocument::Compact));
    }

    db.importFromJson(finalJson);
    items.clear();
    tableWidget->setRowCount(0);
    loadSettings();
    applySavedTrayOptions();

    // 导入后取消所有全局功能
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
