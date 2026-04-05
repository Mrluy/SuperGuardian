#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include <QtWidgets>

using namespace Qt::Literals::StringLiterals;

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
