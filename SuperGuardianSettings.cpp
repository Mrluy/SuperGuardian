#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
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
        QString shortcutArgs;
        item.targetPath = resolveShortcut(path, &shortcutArgs);
        item.launchArgs = s.value("launchArgs").toString();
        if (item.launchArgs.isEmpty() && !shortcutArgs.isEmpty())
            item.launchArgs = shortcutArgs;
        item.processName = QFileInfo(item.targetPath).fileName();
        item.guarding = s.value("guard").toBool();
        item.startTime = QDateTime::fromString(s.value("startTime").toString());
        item.lastRestart = QDateTime::fromString(s.value("lastRestart").toString());
        item.restartCount = 0;
        item.startDelaySecs = s.value("startDelaySecs", 1).toInt();
        if (item.startDelaySecs < 0) item.startDelaySecs = 0;

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

        item.pinned = s.value("pinned", false).toBool();
        item.note = s.value("note").toString();
        item.insertionOrder = s.value("insertionOrder", i).toInt();
        item.guardStartTime = QDateTime::fromString(s.value("guardStartTime").toString());

        items.append(item);
    }
    s.endArray();

    rebuildTableFromItems();
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
        s.setValue("launchArgs", items[i].launchArgs);
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

        s.setValue("pinned", items[i].pinned);
        s.setValue("note", items[i].note);
        s.setValue("insertionOrder", items[i].insertionOrder);
        s.setValue("guardStartTime", items[i].guardStartTime.toString());
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
    if (!showMessageDialog(this, QString::fromUtf8("清空列表"), QString::fromUtf8("确认清空列表中的所有项吗？"), true)) {
        return;
    }

    for (int i = items.size() - 1; i >= 0; --i) {
        int row = findRowByPath(items[i].path);
        if (row >= 0) tableWidget->removeRow(row);
        items.removeAt(i);
    }
    rebuildTableFromItems();
    saveSettings();
}

// ---- Column width management ----

void SuperGuardian::distributeColumnWidths() {
    if (!tableWidget) return;
    autoResizingColumns = true;
    int available = tableWidget->viewport()->width() - tableWidget->columnWidth(9);
    if (available <= 100) { autoResizingColumns = false; return; }

    const double defaultWeights[] = {3.0, 1.0, 1.5, 2.0, 1.0, 1.5, 1.5, 2.0, 1.0};
    double ratios[9];
    bool hasCustom = false;

    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    if (s.contains("columnRatios")) {
        QStringList parts = s.value("columnRatios").toString().split(",");
        if (parts.size() == 9) {
            hasCustom = true;
            double sum = 0;
            for (int i = 0; i < 9; i++) { ratios[i] = parts[i].toDouble(); sum += ratios[i]; }
            if (sum <= 0.001) hasCustom = false;
            else for (int i = 0; i < 9; i++) ratios[i] /= sum;
        }
    }
    if (!hasCustom) {
        double sum = 0;
        for (int i = 0; i < 9; i++) { ratios[i] = defaultWeights[i]; sum += ratios[i]; }
        for (int i = 0; i < 9; i++) ratios[i] /= sum;
    }

    // 仅分配可见列的宽度
    double visibleSum = 0;
    for (int i = 0; i < 9; i++) {
        if (!tableWidget->isColumnHidden(i)) visibleSum += ratios[i];
    }
    if (visibleSum <= 0.001) { autoResizingColumns = false; return; }

    int remaining = available;
    int lastVisible = -1;
    for (int i = 0; i < 9; i++) {
        if (!tableWidget->isColumnHidden(i)) lastVisible = i;
    }
    for (int i = 0; i < 9; i++) {
        if (tableWidget->isColumnHidden(i)) continue;
        if (i == lastVisible) {
            tableWidget->setColumnWidth(i, qMax(40, remaining));
        } else {
            int w = qMax(40, (int)(available * ratios[i] / visibleSum));
            tableWidget->setColumnWidth(i, w);
            remaining -= w;
        }
    }
    autoResizingColumns = false;
}

void SuperGuardian::saveColumnWidths() {
    if (autoResizingColumns) return;
    double total = 0;
    for (int i = 0; i < 9; i++) total += tableWidget->columnWidth(i);
    if (total <= 0) return;
    QStringList parts;
    for (int i = 0; i < 9; i++)
        parts.append(QString::number(tableWidget->columnWidth(i) / total, 'f', 6));
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("columnRatios", parts.join(","));
}

void SuperGuardian::resetColumnWidths() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.remove("columnRatios");
    distributeColumnWidths();
}

// ---- 列显示/隐藏管理 ----

void SuperGuardian::saveColumnVisibility() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QStringList hidden;
    for (int i = 0; i < 9; i++) {
        if (tableWidget->isColumnHidden(i)) hidden << QString::number(i);
    }
    s.setValue("hiddenColumns", hidden.join(","));
}

void SuperGuardian::restoreColumnVisibility() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    if (!s.contains("hiddenColumns")) {
        tableWidget->setColumnHidden(5, true);
        return;
    }
    QString hidden = s.value("hiddenColumns").toString();
    if (hidden.isEmpty()) return;
    for (const QString& col : hidden.split(",")) {
        int i = col.toInt();
        if (i >= 0 && i < 9) tableWidget->setColumnHidden(i, true);
    }
}

void SuperGuardian::onHeaderContextMenu(const QPoint& pos) {
    QHeaderView* header = tableWidget->horizontalHeader();
    int clickedSection = header->logicalIndexAt(pos);
    if (clickedSection == 9) return;

    QMenu menu(this);
    for (int v = 0; v < header->count(); v++) {
        int i = header->logicalIndex(v);
        if (i == 9) continue;
        QTableWidgetItem* hdr = tableWidget->horizontalHeaderItem(i);
        if (!hdr) continue;
        QAction* act = menu.addAction(hdr->text());
        act->setCheckable(true);
        act->setChecked(!tableWidget->isColumnHidden(i));
        connect(act, &QAction::toggled, this, [this, i](bool checked) {
            tableWidget->setColumnHidden(i, !checked);
            saveColumnVisibility();
            distributeColumnWidths();
        });
    }
    menu.exec(header->mapToGlobal(pos));
}

void SuperGuardian::saveHeaderOrder() {
    QHeaderView* header = tableWidget->horizontalHeader();
    QStringList order;
    for (int v = 0; v < header->count(); v++)
        order << QString::number(header->logicalIndex(v));
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("headerOrder", order.join(","));
}

void SuperGuardian::restoreHeaderOrder() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QString orderStr = s.value("headerOrder").toString();
    if (orderStr.isEmpty()) return;
    QStringList parts = orderStr.split(",");
    QHeaderView* header = tableWidget->horizontalHeader();
    int count = header->count();
    if (parts.size() != count) return;
    m_revertingHeader = true;
    for (int v = 0; v < count; v++) {
        int logical = parts[v].toInt();
        if (logical < 0 || logical >= count) continue;
        int currentVisual = header->visualIndex(logical);
        if (currentVisual != v) header->moveSection(currentVisual, v);
    }
    int opVisual = header->visualIndex(9);
    if (opVisual != count - 1) header->moveSection(opVisual, count - 1);
    m_revertingHeader = false;
}

void SuperGuardian::resetHeaderDisplay() {
    QHeaderView* header = tableWidget->horizontalHeader();
    m_revertingHeader = true;
    for (int i = 0; i < header->count(); i++) {
        int curVisual = header->visualIndex(i);
        if (curVisual != i) header->moveSection(curVisual, i);
    }
    for (int i = 0; i < 9; i++)
        tableWidget->setColumnHidden(i, i == 5);
    m_revertingHeader = false;
    saveHeaderOrder();
    saveColumnVisibility();
    distributeColumnWidths();
}
