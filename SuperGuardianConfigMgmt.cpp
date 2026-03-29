#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include <QtWidgets>

// ---- 配置导入导出重置、列表重建 ----

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
