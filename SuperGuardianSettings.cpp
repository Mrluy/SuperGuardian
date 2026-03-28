#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include <QtWidgets>

// ---- 配置持久化、搜索辅助、列宽管理、导入导出重置 ----

void SuperGuardian::loadSettings() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    int size = s.beginReadArray("items");
    for (int i = 0; i < size; ++i) {
        s.setArrayIndex(i);
        QString path = s.value("path").toString();
        // add to list and UI
        GuardItem item;
        item.path = path;
        item.targetPath = resolveShortcut(path);
        item.processName = QFileInfo(item.targetPath).fileName();
        item.guarding = s.value("guard").toBool();
        item.startTime = QDateTime::fromString(s.value("startTime").toString());
        item.lastRestart = QDateTime::fromString(s.value("lastRestart").toString());
        item.restartCount = s.value("restartCount").toInt();
        item.scheduledRestartIntervalSecs = s.value("scheduledRestartIntervalSecs", 0).toInt();
        item.nextScheduledRestart = QDateTime::fromString(s.value("nextScheduledRestart").toString());

        items.append(item);
        int row = tableWidget->rowCount();
        tableWidget->insertRow(row);
        auto makeItem = [&](const QString& t){ QTableWidgetItem* it = new QTableWidgetItem(t); it->setFlags(it->flags() & ~Qt::ItemIsEditable); it->setTextAlignment(Qt::AlignCenter); return it; };
        QTableWidgetItem* nameItem = new QTableWidgetItem(item.processName);
        nameItem->setIcon(getFileIcon(item.targetPath));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setData(Qt::UserRole, item.path);
        nameItem->setToolTip(item.processName);
        tableWidget->setItem(row, 0, nameItem);
        QString initStatus;
        if (item.guarding) initStatus = QString::fromUtf8("运行中");
        else if (item.scheduledRestartIntervalSecs > 0) initStatus = QString::fromUtf8("-");
        else initStatus = QString::fromUtf8("未守护");
        tableWidget->setItem(row, 1, makeItem(initStatus));
        tableWidget->setItem(row, 2, makeItem(item.guarding ? QStringLiteral("0") : QStringLiteral("-")));
        tableWidget->setItem(row, 3, makeItem(item.lastRestart.isValid() ? item.lastRestart.toString(QString::fromUtf8("yyyy年M月d日 hh:mm:ss")) : "-"));
        tableWidget->setItem(row, 4, makeItem(QString::number(item.restartCount)));
        tableWidget->setItem(row, 5, makeItem(formatRestartInterval(item.scheduledRestartIntervalSecs)));
        tableWidget->setItem(row, 6, makeItem(item.nextScheduledRestart.isValid() ? item.nextScheduledRestart.toString(QString::fromUtf8("yyyy年M月d日 hh:mm:ss")) : "-"));

        QWidget* opWidget = new QWidget();
        QHBoxLayout* opLay = new QHBoxLayout(opWidget);
        opLay->setContentsMargins(2,0,2,0);
        opLay->setSpacing(4);
        QPushButton* btn = new QPushButton(item.guarding ? QString::fromUtf8("关闭守护") : QString::fromUtf8("开始守护"));
        QPushButton* srBtn = new QPushButton(item.scheduledRestartIntervalSecs > 0 ? QString::fromUtf8("停止定时重启") : QString::fromUtf8("开启定时重启"));
        srBtn->setObjectName(QString("srBtn_%1").arg(item.path));
        opLay->addWidget(btn);
        opLay->addWidget(srBtn);
        tableWidget->setCellWidget(row, 7, opWidget);
        connect(btn, &QPushButton::clicked, [this, path = item.path, btn]() {
            int idx = findItemIndexByPath(path);
            int row = findRowByPath(path);
            if (idx < 0 || row < 0) return;
            GuardItem& it = items[idx];
            it.guarding = !it.guarding;
            btn->setText(it.guarding ? QString::fromUtf8("关闭守护") : QString::fromUtf8("开始守护"));
            if (it.guarding) {
                it.startTime = QDateTime::currentDateTime();
                int count = 0;
                bool running = isProcessRunning(it.processName, count);
                if (!running && count == 0) {
                    launchProgram(it.path);
                    it.lastLaunchTime = QDateTime::currentDateTime();
                }
            } else {
                if (it.scheduledRestartIntervalSecs <= 0) {
                    if (tableWidget->item(row, 1)) tableWidget->item(row, 1)->setText(QString::fromUtf8("未守护"));
                }
                if (tableWidget->item(row, 2)) tableWidget->item(row, 2)->setText("-");
            }
        });
        connect(srBtn, &QPushButton::clicked, [this, path = item.path, srBtn]() {
            int idx = findItemIndexByPath(path);
            if (idx < 0) return;
            int displayRow = findRowByPath(path);
            if (displayRow < 0) return;
            GuardItem& it = items[idx];
            if (it.scheduledRestartIntervalSecs > 0) {
                it.scheduledRestartIntervalSecs = 0;
                it.nextScheduledRestart = QDateTime();
                srBtn->setText(QString::fromUtf8("开启定时重启"));
                if (tableWidget->item(displayRow, 5)) tableWidget->item(displayRow, 5)->setText("-");
                if (tableWidget->item(displayRow, 6)) tableWidget->item(displayRow, 6)->setText("-");
                saveSettings();
            } else {
                contextSetScheduledRestart(QList<int>{displayRow});
            }
        });
    }
    s.endArray();
    syncSelfGuardListEntry(selfGuardAct && selfGuardAct->isChecked());
}

void SuperGuardian::saveSettings() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.beginWriteArray("items");
    int saveIndex = 0;
    for (int i = 0; i < items.size(); ++i) {
        s.setArrayIndex(saveIndex++);
        s.setValue("path", items[i].path);
        s.setValue("guard", items[i].guarding);
        s.setValue("lastRestart", items[i].lastRestart.toString());
        s.setValue("restartCount", items[i].restartCount);
        s.setValue("startTime", items[i].startTime.toString());
        s.setValue("scheduledRestartIntervalSecs", items[i].scheduledRestartIntervalSecs);
        s.setValue("nextScheduledRestart", items[i].nextScheduledRestart.toString());
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
    int available = tableWidget->viewport()->width() - tableWidget->columnWidth(7);
    if (available <= 100) { autoResizingColumns = false; return; }

    const double defaultWeights[] = {3.0, 1.0, 1.5, 2.0, 1.0, 1.5, 2.0};
    double ratios[7];
    bool hasCustom = false;

    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    if (s.contains("columnRatios")) {
        QStringList parts = s.value("columnRatios").toString().split(",");
        if (parts.size() == 7) {
            hasCustom = true;
            double sum = 0;
            for (int i = 0; i < 7; i++) { ratios[i] = parts[i].toDouble(); sum += ratios[i]; }
            if (sum <= 0.001) hasCustom = false;
            else for (int i = 0; i < 7; i++) ratios[i] /= sum;
        }
    }
    if (!hasCustom) {
        double sum = 0;
        for (int i = 0; i < 7; i++) { ratios[i] = defaultWeights[i]; sum += ratios[i]; }
        for (int i = 0; i < 7; i++) ratios[i] /= sum;
    }

    int remaining = available;
    for (int i = 0; i < 6; i++) {
        int w = qMax(40, (int)(available * ratios[i]));
        tableWidget->setColumnWidth(i, w);
        remaining -= w;
    }
    tableWidget->setColumnWidth(6, qMax(40, remaining));
    autoResizingColumns = false;
}

void SuperGuardian::saveColumnWidths() {
    if (autoResizingColumns) return;
    double total = 0;
    for (int i = 0; i < 7; i++) total += tableWidget->columnWidth(i);
    if (total <= 0) return;
    QStringList parts;
    for (int i = 0; i < 7; i++)
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
    auto makeItem = [](const QString& t){ QTableWidgetItem* it = new QTableWidgetItem(t); it->setFlags(it->flags() & ~Qt::ItemIsEditable); it->setTextAlignment(Qt::AlignCenter); return it; };
    for (int i = 0; i < items.size(); ++i) {
        const GuardItem& item = items[i];
        int row = tableWidget->rowCount();
        tableWidget->insertRow(row);
        QTableWidgetItem* nameItem = new QTableWidgetItem(item.processName);
        nameItem->setIcon(getFileIcon(item.targetPath));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setData(Qt::UserRole, item.path);
        nameItem->setToolTip(item.processName);
        tableWidget->setItem(row, 0, nameItem);
        tableWidget->setItem(row, 1, makeItem(item.guarding ? QString::fromUtf8("\u8fd0\u884c\u4e2d") : QString::fromUtf8("\u672a\u5b88\u62a4")));
        tableWidget->setItem(row, 2, makeItem(item.guarding ? QStringLiteral("0") : QStringLiteral("-")));
        tableWidget->setItem(row, 3, makeItem(item.lastRestart.isValid() ? item.lastRestart.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-"));
        tableWidget->setItem(row, 4, makeItem(QString::number(item.restartCount)));
        tableWidget->setItem(row, 5, makeItem(formatRestartInterval(item.scheduledRestartIntervalSecs)));
        tableWidget->setItem(row, 6, makeItem(item.nextScheduledRestart.isValid() ? item.nextScheduledRestart.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-"));
        QWidget* opWidget = new QWidget();
        QHBoxLayout* opLay = new QHBoxLayout(opWidget);
        opLay->setContentsMargins(2,0,2,0);
        opLay->setSpacing(4);
        QPushButton* btn = new QPushButton(item.guarding ? QString::fromUtf8("\u5173\u95ed\u5b88\u62a4") : QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
        QPushButton* srBtn = new QPushButton(item.scheduledRestartIntervalSecs > 0 ? QString::fromUtf8("\u505c\u6b62\u5b9a\u65f6\u91cd\u542f") : QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u91cd\u542f"));
        srBtn->setObjectName(QString("srBtn_%1").arg(item.path));
        opLay->addWidget(btn);
        opLay->addWidget(srBtn);
        tableWidget->setCellWidget(row, 7, opWidget);
    }
}
