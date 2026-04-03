#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <windows.h>

// ---- 配置导入导出重置、列表重建 ----

// 辅助：将 ScheduleRule 列表转为 JSON 数组（导出用）
static QJsonArray exportScheduleRules(const QList<ScheduleRule>& rules) {
    QJsonArray arr;
    for (const ScheduleRule& r : rules) {
        QJsonObject o;
        o[u"type"_s] = (r.type == ScheduleRule::Periodic) ? u"periodic"_s : u"fixed"_s;
        o[u"intervalSecs"_s] = r.intervalSecs;
        o[u"fixedTime"_s] = r.fixedTime.toString(u"HH:mm:ss"_s);
        QJsonArray days;
        for (int d : r.daysOfWeek) days.append(d);
        o[u"daysOfWeek"_s] = days;
        o[u"nextTrigger"_s] = r.nextTrigger.toString(Qt::ISODate);
        arr.append(o);
    }
    return arr;
}

// 辅助：从 JSON 数组解析 ScheduleRule 列表（导入用）
static QList<ScheduleRule> importScheduleRules(const QJsonArray& arr) {
    QList<ScheduleRule> rules;
    for (const QJsonValue& v : arr) {
        QJsonObject o = v.toObject();
        ScheduleRule r;
        r.type = (o[u"type"_s].toString() == u"fixed"_s) ? ScheduleRule::FixedTime : ScheduleRule::Periodic;
        r.intervalSecs = o[u"intervalSecs"_s].toInt(3600);
        r.fixedTime = QTime::fromString(o[u"fixedTime"_s].toString(), u"HH:mm:ss"_s);
        QJsonArray days = o[u"daysOfWeek"_s].toArray();
        for (const QJsonValue& dv : days) r.daysOfWeek.insert(dv.toInt());
        r.nextTrigger = QDateTime::fromString(o[u"nextTrigger"_s].toString(), Qt::ISODate);
        rules.append(r);
    }
    return rules;
}

void SuperGuardian::exportConfig() {
    saveSettings();
    auto& db = ConfigDatabase::instance();

    QJsonObject root;
    root[u"alwaysOnTop"_s] = db.value(u"alwaysOnTop"_s, false).toBool();
    root[u"emailEnabled"_s] = db.value(u"emailEnabled"_s, false).toBool();
    root[u"theme"_s] = db.value(u"theme"_s, u"dark"_s).toString();

    // SMTP 配置
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

    // Items 数组
    QJsonArray itemsArr;
    for (const GuardItem& item : items) {
        QJsonObject o;
        o[u"path"_s] = item.path;
        o[u"note"_s] = item.note;
        o[u"pinned"_s] = item.pinned;
        o[u"insertionOrder"_s] = item.insertionOrder;
        o[u"scheduledRunEnabled"_s] = item.scheduledRunEnabled;
        o[u"startDelaySecs"_s] = item.startDelaySecs;
        o[u"trackRunDuration"_s] = item.trackRunDuration;
        if (!item.launchArgs.isEmpty())
            o[u"launchArgs"_s] = item.launchArgs;

        // guard 子对象
        QJsonObject guardObj;
        guardObj[u"enabled"_s] = item.guarding;
        guardObj[u"startTime"_s] = item.guardStartTime.isValid()
            ? item.guardStartTime.toString(Qt::ISODate) : u""_s;
        o[u"guard"_s] = guardObj;

        // retry 子对象
        QJsonObject retryObj;
        retryObj[u"intervalSecs"_s] = item.retryConfig.retryIntervalSecs;
        retryObj[u"maxDurationSecs"_s] = item.retryConfig.maxDurationSecs;
        retryObj[u"maxRetries"_s] = item.retryConfig.maxRetries;
        o[u"retry"_s] = retryObj;

        // 定时运行规则
        o[u"runRules"_s] = exportScheduleRules(item.runRules);
        // 定时重启规则
        o[u"restartRules"_s] = exportScheduleRules(item.restartRules);
        o[u"restartRulesActive"_s] = item.restartRulesActive;

        // emailNotifications 子对象
        QJsonObject emailObj;
        emailObj[u"enabled"_s] = item.emailNotify.enabled;
        emailObj[u"onGuardTriggered"_s] = item.emailNotify.onGuardTriggered;
        emailObj[u"onProcessExited"_s] = item.emailNotify.onProcessExited;
        emailObj[u"onRestartFailed"_s] = item.emailNotify.onScheduledRestartFailed;
        emailObj[u"onRetryExhausted"_s] = item.emailNotify.onRetryExhausted;
        emailObj[u"onRunFailed"_s] = item.emailNotify.onScheduledRunFailed;
        emailObj[u"onStartFailed"_s] = item.emailNotify.onStartFailed;
        o[u"emailNotifications"_s] = emailObj;

        // status 子对象
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

    // 写入文件
    QString timestamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    QString defaultName = u"SuperGuardian_Config_%1.json"_s.arg(timestamp);
    QString filePath = QFileDialog::getSaveFileName(this,
        u"导出配置"_s, defaultName, u"JSON Files (*.json)"_s);
    if (filePath.isEmpty()) return;

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

// 将结构化 JSON item 转为内部扁平格式 item（供 ConfigDatabase 存储）
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

    // guard 子对象 → 扁平字段
    if (src.contains(u"guard"_s) && src[u"guard"_s].isObject()) {
        QJsonObject g = src[u"guard"_s].toObject();
        o[u"guard"_s] = g[u"enabled"_s].toBool();
        o[u"guardStartTime"_s] = g[u"startTime"_s].toString();
    } else {
        o[u"guard"_s] = src[u"guard"_s].toBool();
    }

    // retry 子对象 → 扁平字段
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

    // 定时规则：直接存储为 JSON 字符串
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

    // emailNotifications 子对象 → 扁平字段
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
        // 旧扁平格式直接保留
        o[u"emailNotifyEnabled"_s] = src[u"emailNotifyEnabled"_s].toBool();
        o[u"emailOnGuardTriggered"_s] = src[u"emailOnGuardTriggered"_s].toBool();
        o[u"emailOnStartFailed"_s] = src.contains(u"emailOnStartFailed"_s) ? src[u"emailOnStartFailed"_s].toBool() : true;
        o[u"emailOnRestartFailed"_s] = src.contains(u"emailOnRestartFailed"_s) ? src[u"emailOnRestartFailed"_s].toBool() : true;
        o[u"emailOnRunFailed"_s] = src.contains(u"emailOnRunFailed"_s) ? src[u"emailOnRunFailed"_s].toBool() : true;
        o[u"emailOnProcessExited"_s] = src[u"emailOnProcessExited"_s].toBool();
        o[u"emailOnRetryExhausted"_s] = src.contains(u"emailOnRetryExhausted"_s) ? src[u"emailOnRetryExhausted"_s].toBool() : true;
    }

    // status 子对象 → 扁平字段
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
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    if (!root.contains(u"items"_s)) return false;

    // 判断是新结构化格式还是旧扁平格式
    QJsonValue itemsVal = root[u"items"_s];

    if (itemsVal.isArray()) {
        // 新结构化格式：items 是 JSON 数组
        QJsonArray srcArr = itemsVal.toArray();
        QJsonArray flatArr;
        for (const QJsonValue& v : srcArr) {
            if (!v.isObject()) continue;
            QJsonObject srcItem = v.toObject();
            if (srcItem[u"path"_s].toString().isEmpty()) continue;
            // 检查是否有嵌套子对象（新格式标志）
            if (srcItem.contains(u"guard"_s) && srcItem[u"guard"_s].isObject()) {
                flatArr.append(structuredItemToFlat(srcItem));
            } else {
                // 已经是扁平格式，直接使用
                flatArr.append(srcItem);
            }
        }

        // 构建 ConfigDatabase 的扁平导入格式
        outJson = QJsonObject();
        outJson[u"items"_s] = QString::fromUtf8(
            QJsonDocument(flatArr).toJson(QJsonDocument::Compact));

        // 导入顶级设置
        if (root.contains(u"alwaysOnTop"_s))
            outJson[u"alwaysOnTop"_s] = root[u"alwaysOnTop"_s].toBool();
        if (root.contains(u"emailEnabled"_s))
            outJson[u"emailEnabled"_s] = root[u"emailEnabled"_s].toBool();
        if (root.contains(u"theme"_s))
            outJson[u"theme"_s] = root[u"theme"_s].toString();

        // 导入 SMTP（嵌套对象 → 扁平 key）
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
        // 旧扁平格式：items 是 JSON 字符串
        outJson = root;
        return true;
    }

    return false;
}

static bool tryImportIni(const QString& filePath, QJsonObject& outJson) {
    QSettings s(filePath, QSettings::IniFormat);
    if (s.status() != QSettings::NoError) return false;

    int size = s.beginReadArray("items");
    if (size <= 0) { s.endArray(); return false; }

    QJsonArray itemsArr;
    for (int i = 0; i < size; ++i) {
        s.setArrayIndex(i);
        if (!s.contains("path") || s.value("path").toString().isEmpty()) {
            s.endArray();
            return false;
        }
        QJsonObject item;
        for (const QString& key : s.childKeys()) {
            item[key] = QJsonValue::fromVariant(s.value(key));
        }
        itemsArr.append(item);
    }
    s.endArray();

    // 构建完整的 JSON 配置对象
    outJson = QJsonObject();
    outJson[u"items"_s] = QString::fromUtf8(QJsonDocument(itemsArr).toJson(QJsonDocument::Compact));

    // 迁移 SMTP 配置
    s.beginGroup("smtp");
    for (const QString& key : s.childKeys()) {
        outJson[u"smtp/"_s + key] = QJsonValue::fromVariant(s.value(key));
    }
    s.endGroup();

    // 迁移其他顶级设置
    QStringList topKeys = { "emailEnabled", "duplicateWhitelist", "theme", "minimizeToTray",
        "columnWidths", "sortSection", "sortState", "hiddenColumns", "headerOrder" };
    for (const QString& key : topKeys) {
        if (s.contains(key))
            outJson[key] = QJsonValue::fromVariant(s.value(key));
    }

    return true;
}

void SuperGuardian::importConfig() {
    QString filePath = QFileDialog::getOpenFileName(this,
        u"导入配置"_s, "", "配置文件 (*.json *.ini);;JSON Files (*.json);;INI Files (*.ini)");
    if (filePath.isEmpty()) return;

    QJsonObject json;
    bool ok = tryImportJson(filePath, json);
    if (!ok) ok = tryImportIni(filePath, json);

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

    showMessageDialog(this, u"导入配置"_s,
        u"配置已成功导入。"_s);
    logOperation(u"导入配置从 %1"_s.arg(filePath));
}

void SuperGuardian::resetConfig() {
    if (!showMessageDialog(this, u"重置配置"_s,
        u"确认重置全部配置吗？此操作将清除所有设置和程序列表。"_s, true))
        return;

    // 清空数据库配置
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

    showMessageDialog(this, u"重置配置"_s,
        u"配置已重置为默认设置。"_s);
    logOperation(u"重置全部配置"_s);
}

void SuperGuardian::rebuildTableFromItems() {
    tableWidget->setRowCount(0);
    // Pinned items first
    for (const GuardItem& item : items) {
        if (item.pinned) {
            int row = tableWidget->rowCount();
            tableWidget->insertRow(row);
            setupTableRow(row, item);
        }
    }
    // Then unpinned
    for (const GuardItem& item : items) {
        if (!item.pinned) {
            int row = tableWidget->rowCount();
            tableWidget->insertRow(row);
            setupTableRow(row, item);
        }
    }
}

// ---- 备注 ----

void SuperGuardian::contextSetNote(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"备注"_s);
    dlg.setFixedWidth(400);
    dlg.setMinimumHeight(140);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"请输入备注名称（留空表示清除备注）："_s));
    QLineEdit* noteEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) noteEdit->setText(items[itemIdx].note);
    }
    lay->addWidget(noteEdit);
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
    QString note = noteEdit->text().trimmed();
    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        items[itemIdx].note = note;
        QString displayName = note.isEmpty()
            ? (items[itemIdx].launchArgs.isEmpty() ? items[itemIdx].processName : (items[itemIdx].processName + " " + items[itemIdx].launchArgs))
            : note;
        QString tooltipName = items[itemIdx].launchArgs.isEmpty() ? items[itemIdx].processName : (items[itemIdx].processName + " " + items[itemIdx].launchArgs);
        if (tableWidget->item(row, 0)) {
            tableWidget->item(row, 0)->setText(displayName);
            tableWidget->item(row, 0)->setToolTip(tooltipName);
        }
    }
    saveSettings();
}

// ---- 桌面快捷方式 ----

void SuperGuardian::createDesktopShortcut() {
    QString exePath = QCoreApplication::applicationFilePath();
    QString exeDir = QCoreApplication::applicationDirPath();
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString lnkPath = desktop + u"/超级守护.lnk"_s;
    QString ps = QString(
        "$ws = New-Object -ComObject WScript.Shell; "
        "$sc = $ws.CreateShortcut('%1'); "
        "$sc.TargetPath = '%2'; "
        "$sc.WorkingDirectory = '%3'; "
        "$sc.IconLocation = '%2,0'; "
        "$sc.Save()"
    ).arg(lnkPath.replace("'", "''"),
          QDir::toNativeSeparators(exePath).replace("'", "''"),
          QDir::toNativeSeparators(exeDir).replace("'", "''"));
    QProcess proc;
    proc.start("powershell", QStringList() << "-NoProfile" << "-Command" << ps);
    proc.waitForFinished(10000);
    if (proc.exitCode() == 0) {
        showMessageDialog(this, u"桌面快捷方式"_s,
            u"桌面快捷方式已创建：超级守护"_s);
    } else {
        showMessageDialog(this, u"桌面快捷方式"_s,
            u"创建快捷方式失败，请检查权限。"_s);
    }
}

// ---- 排序管理 ----

void SuperGuardian::performSort() {
    if (sortState == 0 || activeSortSection < 0 || activeSortSection >= 9) {
        std::sort(items.begin(), items.end(), [](const GuardItem& a, const GuardItem& b) {
            if (a.pinned != b.pinned) return a.pinned > b.pinned;
            return a.insertionOrder < b.insertionOrder;
        });
        rebuildTableFromItems();
        return;
    }

    Qt::SortOrder order = (sortState == 1) ? Qt::AscendingOrder : Qt::DescendingOrder;

    if (tableWidget->rowCount() == 0 && !items.isEmpty())
        rebuildTableFromItems();

    auto collectRows = [&](bool pinned) -> QVector<QPair<QString, int>> {
        QVector<QPair<QString, int>> rows;
        for (int r = 0; r < tableWidget->rowCount(); r++) {
            int idx = findItemIndexByPath(rowPath(r));
            if (idx < 0) continue;
            if (items[idx].pinned != pinned) continue;
            QTableWidgetItem* it = tableWidget->item(r, activeSortSection);
            rows.append({ it ? it->text() : QString(), idx });
        }
        return rows;
    };
    auto sortRows = [&](QVector<QPair<QString, int>>& rows) {
        std::sort(rows.begin(), rows.end(), [order](const QPair<QString, int>& a, const QPair<QString, int>& b) {
            return (order == Qt::AscendingOrder) ? (a.first.localeAwareCompare(b.first) < 0)
                                                 : (a.first.localeAwareCompare(b.first) > 0);
        });
    };

    auto pinnedRows = collectRows(true);
    auto unpinnedRows = collectRows(false);
    sortRows(pinnedRows);
    sortRows(unpinnedRows);

    QVector<GuardItem> newItems;
    for (const auto& p : pinnedRows) newItems.append(items[p.second]);
    for (const auto& p : unpinnedRows) newItems.append(items[p.second]);
    items = newItems;
    rebuildTableFromItems();

    QHeaderView* header = tableWidget->horizontalHeader();
    header->setSortIndicatorShown(true);
    header->setSortIndicator(activeSortSection, order);
}

void SuperGuardian::saveSortState() {
    auto& db = ConfigDatabase::instance();
    db.setValue(u"sortSection"_s, activeSortSection);
    db.setValue(u"sortState"_s, sortState);
}

// ---- 诊断信息导出（AI 辅助调试） ----

void SuperGuardian::exportDiagnosticInfo() {
    QString timestamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    QString defaultName = u"SuperGuardian_Diagnostic_%1.txt"_s.arg(timestamp);
    QString filePath = QFileDialog::getSaveFileName(this,
        u"导出诊断信息"_s, defaultName, u"文本文件 (*.txt)"_s);
    if (filePath.isEmpty()) return;

    QStringList lines;
    auto section = [&](const QString& title) {
        lines << QString();
        lines << u"═══════════════════════════════════════════════════════"_s;
        lines << u"  %1"_s.arg(title);
        lines << u"═══════════════════════════════════════════════════════"_s;
    };

    // 系统与应用信息
    section(u"系统与应用信息"_s);
    lines << u"导出时间: %1"_s.arg(QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s));
    lines << u"应用版本: %1"_s.arg(QCoreApplication::applicationVersion());
    lines << u"Qt 版本: %1"_s.arg(qVersion());
    lines << u"操作系统: %1"_s.arg(QSysInfo::prettyProductName());
    lines << u"CPU 架构: %1"_s.arg(QSysInfo::currentCpuArchitecture());
    lines << u"应用路径: %1"_s.arg(QCoreApplication::applicationFilePath());
    lines << u"数据目录: %1"_s.arg(appDataDirPath());
    lines << u"PID: %1"_s.arg(QCoreApplication::applicationPid());
    // 编码验证：运行时检查 u"" 字面量→UTF-8 转换是否正确
    {
        QByteArray probe = u"中文"_s.toUtf8();
        QString hex;
        for (int i = 0; i < probe.size(); ++i) {
            if (i) hex += u' ';
            hex += QString::asprintf("%02X", static_cast<unsigned char>(probe[i]));
        }
        lines << u"编码验证: \"%1\" → [%2] (预期: E4 B8 AD E6 96 87)"_s
            .arg(u"中文"_s, hex);
    }

    // 配置信息
    section(u"当前配置"_s);
    auto& db = ConfigDatabase::instance();
    QJsonObject allConfig = db.exportToJson();
    for (auto it = allConfig.begin(); it != allConfig.end(); ++it) {
        QString val = it.value().isString() ? it.value().toString()
            : QString::fromUtf8(QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact));
        // 截断过长的值
        if (val.length() > 200) val = val.left(200) + u"... (已截断)"_s;
        lines << u"  %1 = %2"_s.arg(it.key(), val);
    }

    // 守护项状态
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
        lines << u"  重试配置: 间隔%1秒, 最多%2次, 最长%3秒"_s.arg(item.retryConfig.retryIntervalSecs)
            .arg(item.retryConfig.maxRetries).arg(item.retryConfig.maxDurationSecs);
        if (item.retryActive)
            lines << u"  重试中: 当前第%1次, 开始于 %2"_s.arg(item.currentRetryCount)
                .arg(item.retryStartTime.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        int procCount = 0;
        bool running = isProcessRunning(item.processName, procCount);
        lines << u"  当前进程状态: %1 (实例数: %2)"_s.arg(running ? u"运行中"_s : u"未运行"_s).arg(procCount);
    }

    // 自我守护状态
    section(u"自我守护状态"_s);
    bool selfGuardEnabled = db.value(u"self_guard_enabled"_s, false).toBool();
    bool manualExit = db.value(u"self_guard_manual_exit"_s, false).toBool();
    int watchdogPid = db.value(u"watchdog_pid"_s, 0).toInt();
    lines << u"  自我守护: %1"_s.arg(selfGuardEnabled ? u"启用"_s : u"停用"_s);
    lines << u"  手动退出标记: %1"_s.arg(manualExit ? u"是"_s : u"否"_s);
    lines << u"  看门狗 PID: %1"_s.arg(watchdogPid > 0 ? QString::number(watchdogPid) : u"无"_s);
    if (watchdogPid > 0) {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)watchdogPid);
        lines << u"  看门狗进程存活: %1"_s.arg(h ? u"是"_s : u"否"_s);
        if (h) CloseHandle(h);
    }

    // 最近日志
    auto appendLogs = [&](const QString& category, const QString& title, int limit) {
        section(u"最近%1 (最多%2条)"_s.arg(title).arg(limit));
        auto logs = LogDatabase::instance().queryLogs(category, limit);
        if (logs.isEmpty()) {
            lines << u"  (无记录)"_s;
        } else {
            for (const LogEntry& entry : logs) {
                QString prog = entry.program.isEmpty() ? QString() : u" [%1]"_s.arg(entry.program);
                lines << u"  %1%2 %3"_s.arg(entry.timestamp.toString(u"MM-dd HH:mm:ss"_s), prog, entry.message);
            }
        }
    };
    appendLogs(u"runtime"_s, u"运行日志"_s, 50);
    appendLogs(u"operation"_s, u"操作日志"_s, 30);
    appendLogs(u"guard"_s, u"守护日志"_s, 30);
    appendLogs(u"scheduled_restart"_s, u"定时重启日志"_s, 20);
    appendLogs(u"scheduled_run"_s, u"定时运行日志"_s, 20);

    // 写入文件（显式 UTF-8 + BOM，避免系统编码导致乱码）
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
