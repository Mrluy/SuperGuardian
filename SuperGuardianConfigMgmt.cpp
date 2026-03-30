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

// ---- 本地更新 ----

void SuperGuardian::showUpdateDialog() {
    QDialog dialog(this, kDialogFlags);
    dialog.setWindowTitle(QString::fromUtf8("\u8f6f\u4ef6\u66f4\u65b0"));
    dialog.setMinimumWidth(500);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(QString::fromUtf8(
        "\u9009\u62e9\u65b0\u7248\u672c\u7684 SuperGuardian.exe \u8fdb\u884c\u66f4\u65b0\u3002\n"
        "\u66f4\u65b0\u65f6\u5c06\u81ea\u52a8\u5907\u4efd\u65e7\u7248\u672c\u5230 bak \u6587\u4ef6\u5939\uff0c\u6700\u591a\u4fdd\u7559 5 \u4e2a\u65e7\u7248\u672c\u3002")));

    QHBoxLayout* fileLayout = new QHBoxLayout();
    QLineEdit* fileEdit = new QLineEdit();
    fileEdit->setPlaceholderText(QString::fromUtf8("\u9009\u62e9\u65b0\u7248\u672c SuperGuardian.exe \u7684\u8def\u5f84"));
    fileEdit->setReadOnly(true);
    QPushButton* browseBtn = new QPushButton(QString::fromUtf8("\u6d4f\u89c8"));
    fileLayout->addWidget(fileEdit);
    fileLayout->addWidget(browseBtn);
    layout->addLayout(fileLayout);

    layout->addStretch();
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* updateBtn = new QPushButton(QString::fromUtf8("\u5f00\u59cb\u66f4\u65b0"));
    updateBtn->setEnabled(false);
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
    btnLayout->addWidget(updateBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(browseBtn, &QPushButton::clicked, &dialog, [&]() {
        QString file = QFileDialog::getOpenFileName(&dialog,
            QString::fromUtf8("\u9009\u62e9\u65b0\u7248\u672c\u7a0b\u5e8f"), "", "Executable (*.exe)");
        if (!file.isEmpty()) {
            fileEdit->setText(file);
            updateBtn->setEnabled(true);
        }
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    connect(updateBtn, &QPushButton::clicked, &dialog, [&]() {
        QString newExe = fileEdit->text();
        if (newExe.isEmpty()) return;

        QString currentExe = QCoreApplication::applicationFilePath();
        QString appDir = QCoreApplication::applicationDirPath();
        QString bakDir = QDir(appDir).filePath("bak");
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString bakSubDir = QDir(bakDir).filePath(timestamp);
        QDir().mkpath(bakSubDir);

        QString bakPath = QDir(bakSubDir).filePath("SuperGuardian.exe.bak");
        if (!QFile::rename(currentExe, bakPath)) {
            showMessageDialog(&dialog, QString::fromUtf8("\u66f4\u65b0\u5931\u8d25"),
                QString::fromUtf8("\u65e0\u6cd5\u5907\u4efd\u5f53\u524d\u7a0b\u5e8f\u6587\u4ef6\u3002"));
            return;
        }
        if (!QFile::copy(newExe, currentExe)) {
            QFile::rename(bakPath, currentExe);
            showMessageDialog(&dialog, QString::fromUtf8("\u66f4\u65b0\u5931\u8d25"),
                QString::fromUtf8("\u65e0\u6cd5\u590d\u5236\u65b0\u7248\u672c\u7a0b\u5e8f\u3002"));
            return;
        }

        // 清理旧备份，最多保留5个
        QDir bakDirObj(bakDir);
        QStringList entries = bakDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        while (entries.size() > 5) {
            QDir(bakDirObj.filePath(entries.takeFirst())).removeRecursively();
        }

        dialog.accept();

        if (showMessageDialog(this, QString::fromUtf8("\u66f4\u65b0\u6210\u529f"),
            QString::fromUtf8("\u7a0b\u5e8f\u5df2\u66f4\u65b0\u6210\u529f\u3002\u65e7\u7248\u672c\u5df2\u5907\u4efd\u5230 bak/%1/\n\u662f\u5426\u7acb\u5373\u91cd\u542f\u8f6f\u4ef6\u4ee5\u5e94\u7528\u66f4\u65b0\uff1f").arg(timestamp), true)) {
            QProcess::startDetached(currentExe, QStringList());
            onExit();
        }
    });

    dialog.exec();
}

// ---- 备注 ----

void SuperGuardian::contextSetNote(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u5907\u6ce8"));
    dlg.setFixedWidth(400);
    dlg.setMinimumHeight(140);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u8bf7\u8f93\u5165\u5907\u6ce8\u540d\u79f0\uff08\u7559\u7a7a\u8868\u793a\u6e05\u9664\u5907\u6ce8\uff09\uff1a")));
    QLineEdit* noteEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) noteEdit->setText(items[itemIdx].note);
    }
    lay->addWidget(noteEdit);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
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
    QString lnkPath = desktop + QString::fromUtf8("/\u8d85\u7ea7\u5b88\u62a4.lnk");
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
        showMessageDialog(this, QString::fromUtf8("\u684c\u9762\u5feb\u6377\u65b9\u5f0f"),
            QString::fromUtf8("\u684c\u9762\u5feb\u6377\u65b9\u5f0f\u5df2\u521b\u5efa\uff1a\u8d85\u7ea7\u5b88\u62a4"));
    } else {
        showMessageDialog(this, QString::fromUtf8("\u684c\u9762\u5feb\u6377\u65b9\u5f0f"),
            QString::fromUtf8("\u521b\u5efa\u5feb\u6377\u65b9\u5f0f\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u6743\u9650\u3002"));
    }
}

// ---- 排序管理 ----

void SuperGuardian::performSort() {
    if (sortState == 0 || activeSortSection < 0 || activeSortSection >= 8) {
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
