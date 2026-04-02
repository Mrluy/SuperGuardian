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
        u"导出配置"_s, defaultName, "INI Files (*.ini)");
    if (filePath.isEmpty()) return;
    if (QFile::exists(filePath)) QFile::remove(filePath);
    if (QFile::copy(appSettingsFilePath(), filePath)) {
        showMessageDialog(this, u"导出配置"_s,
            u"配置已导出到：\n%1"_s.arg(filePath));
    } else {
        showMessageDialog(this, u"导出失败"_s,
            u"无法写入文件：%1"_s.arg(filePath));
    }
}

void SuperGuardian::importConfig() {
    QString filePath = QFileDialog::getOpenFileName(this,
        u"导入配置"_s, "", "INI Files (*.ini)");
    if (filePath.isEmpty()) return;

    QSettings imported(filePath, QSettings::IniFormat);
    if (imported.status() != QSettings::NoError) {
        showMessageDialog(this, u"导入失败"_s,
            u"配置文件格式无效。"_s);
        return;
    }
    int size = imported.beginReadArray("items");
    if (size < 0) {
        imported.endArray();
        showMessageDialog(this, u"导入失败"_s,
            u"配置文件不包含有效的程序列表。"_s);
        return;
    }
    for (int i = 0; i < size; i++) {
        imported.setArrayIndex(i);
        if (!imported.contains("path") || imported.value("path").toString().isEmpty()) {
            imported.endArray();
            showMessageDialog(this, u"导入失败"_s,
                u"配置文件中第 %1 个程序项缺少路径信息。"_s.arg(i+1));
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

    showMessageDialog(this, u"导入配置"_s,
        u"配置已成功导入。"_s);
}

void SuperGuardian::resetConfig() {
    if (!showMessageDialog(this, u"重置配置"_s,
        u"确认重置全部配置吗？此操作将清除所有设置和程序列表。"_s, true))
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

    showMessageDialog(this, u"重置配置"_s,
        u"配置已重置为默认设置。"_s);
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
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("sortSection", activeSortSection);
    s.setValue("sortState", sortState);
}
