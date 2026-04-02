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
        "\u9009\u62e9\u65b0\u7248\u672c\u7684 SuperGuardian.exe \u6216 ZIP \u538b\u7f29\u5305\u8fdb\u884c\u66f4\u65b0\u3002\n"
        "\u652f\u6301\u62d6\u653e\u6587\u4ef6\u5230\u8f93\u5165\u6846\u3002\n"
        "\u66f4\u65b0\u65f6\u5c06\u81ea\u52a8\u5907\u4efd\u65e7\u7248\u672c\u5230 bak \u6587\u4ef6\u5939\uff0c\u6700\u591a\u4fdd\u7559 5 \u4e2a\u65e7\u7248\u672c\u3002")));

    QHBoxLayout* fileLayout = new QHBoxLayout();

    class UpdatePathLineEdit : public QLineEdit {
    public:
        UpdatePathLineEdit(QWidget* p=nullptr) : QLineEdit(p) { setAcceptDrops(true); }
    protected:
        void dragEnterEvent(QDragEnterEvent* e) override {
            if (e->mimeData()->hasUrls()) {
                for (const QUrl& url : e->mimeData()->urls()) {
                    QString path = url.toLocalFile().toLower();
                    if (path.endsWith(".exe") || path.endsWith(".zip")) {
                        e->acceptProposedAction();
                        return;
                    }
                }
            }
        }
        void dropEvent(QDropEvent* e) override {
            for (const QUrl& url : e->mimeData()->urls()) {
                QString path = url.toLocalFile();
                QString lower = path.toLower();
                if (lower.endsWith(".exe") || lower.endsWith(".zip")) {
                    setText(path);
                    return;
                }
            }
        }
    };

    UpdatePathLineEdit* fileEdit = new UpdatePathLineEdit();
    fileEdit->setPlaceholderText(QString::fromUtf8("\u9009\u62e9\u6216\u62d6\u653e\u65b0\u7248\u672c .exe \u6216 .zip \u6587\u4ef6"));
    QPushButton* browseBtn = new QPushButton(QString::fromUtf8("\u6d4f\u89c8"));
    fileLayout->addWidget(fileEdit);
    fileLayout->addWidget(browseBtn);
    layout->addLayout(fileLayout);

    layout->addStretch();

    // 恢复旧版本区域
    QHBoxLayout* restoreLayout = new QHBoxLayout();
    restoreLayout->addStretch();
    QPushButton* restoreBtn = new QPushButton(QString::fromUtf8("\u6062\u590d\u65e7\u7248\u672c"));
    restoreLayout->addWidget(restoreBtn);
    layout->addLayout(restoreLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* updateBtn = new QPushButton(QString::fromUtf8("\u5f00\u59cb\u66f4\u65b0"));
    updateBtn->setEnabled(false);
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
    btnLayout->addWidget(updateBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(fileEdit, &QLineEdit::textChanged, &dialog, [updateBtn, fileEdit]() {
        QString path = fileEdit->text().trimmed().toLower();
        updateBtn->setEnabled(!path.isEmpty() && (path.endsWith(".exe") || path.endsWith(".zip")));
    });

    connect(browseBtn, &QPushButton::clicked, &dialog, [&]() {
        QString file = QFileDialog::getOpenFileName(&dialog,
            QString::fromUtf8("\u9009\u62e9\u65b0\u7248\u672c\u7a0b\u5e8f"), "",
            QString::fromUtf8("Executable / ZIP (*.exe *.zip);;All Files (*)"));
        if (!file.isEmpty()) {
            QString lower = file.toLower();
            if (lower.endsWith(".exe") || lower.endsWith(".zip"))
                fileEdit->setText(file);
            else
                showMessageDialog(&dialog, QString::fromUtf8("\u63d0\u793a"),
                    QString::fromUtf8("\u4ec5\u652f\u6301 .exe \u548c .zip \u6587\u4ef6\u3002"));
        }
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    // 恢复旧版本
    connect(restoreBtn, &QPushButton::clicked, &dialog, [&]() {
        QString appDir = QCoreApplication::applicationDirPath();
        QString bakDir = QDir(appDir).filePath("bak");
        QDir bakDirObj(bakDir);
        if (!bakDirObj.exists()) {
            showMessageDialog(&dialog, QString::fromUtf8("\u63d0\u793a"),
                QString::fromUtf8("\u6ca1\u6709\u627e\u5230\u4efb\u4f55\u5907\u4efd\u7248\u672c\u3002"));
            return;
        }
        QStringList entries = bakDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        if (entries.isEmpty()) {
            showMessageDialog(&dialog, QString::fromUtf8("\u63d0\u793a"),
                QString::fromUtf8("\u6ca1\u6709\u627e\u5230\u4efb\u4f55\u5907\u4efd\u7248\u672c\u3002"));
            return;
        }

        QDialog restoreDlg(&dialog, kDialogFlags);
        restoreDlg.setWindowTitle(QString::fromUtf8("\u6062\u590d\u65e7\u7248\u672c"));
        restoreDlg.setMinimumSize(360, 300);
        QVBoxLayout* rlay = new QVBoxLayout(&restoreDlg);
        rlay->addWidget(new QLabel(QString::fromUtf8("\u9009\u62e9\u8981\u6062\u590d\u7684\u5907\u4efd\u7248\u672c\uff1a")));
        QListWidget* backupList = new QListWidget();
        for (const QString& entry : entries) {
            QString bakExe = QDir(bakDirObj.filePath(entry)).filePath("SuperGuardian.exe.bak");
            if (QFile::exists(bakExe)) {
                QString displayDate = entry;
                if (entry.length() == 15 && entry.contains('_')) {
                    QDateTime dt = QDateTime::fromString(entry, "yyyyMMdd_HHmmss");
                    if (dt.isValid())
                        displayDate = dt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 HH:mm:ss"));
                }
                QListWidgetItem* item = new QListWidgetItem(displayDate);
                item->setData(Qt::UserRole, entry);
                backupList->addItem(item);
            }
        }
        rlay->addWidget(backupList);
        QHBoxLayout* rbtnLay = new QHBoxLayout();
        rbtnLay->addStretch();
        QPushButton* restoreOkBtn = new QPushButton(QString::fromUtf8("\u6062\u590d"));
        QPushButton* restoreCancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
        restoreOkBtn->setEnabled(false);
        QObject::connect(backupList, &QListWidget::currentRowChanged, [restoreOkBtn](int row) {
            restoreOkBtn->setEnabled(row >= 0);
        });
        QObject::connect(restoreOkBtn, &QPushButton::clicked, &restoreDlg, &QDialog::accept);
        QObject::connect(restoreCancelBtn, &QPushButton::clicked, &restoreDlg, &QDialog::reject);
        rbtnLay->addWidget(restoreOkBtn); rbtnLay->addWidget(restoreCancelBtn);
        rlay->addLayout(rbtnLay);

        if (restoreDlg.exec() != QDialog::Accepted) return;
        QListWidgetItem* selected = backupList->currentItem();
        if (!selected) return;
        QString selectedDir = selected->data(Qt::UserRole).toString();
        QString bakExe = QDir(bakDirObj.filePath(selectedDir)).filePath("SuperGuardian.exe.bak");
        QString currentExe = QCoreApplication::applicationFilePath();

        QString newBakDir = QDir(bakDir).filePath(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        QDir().mkpath(newBakDir);
        QString newBakPath = QDir(newBakDir).filePath("SuperGuardian.exe.bak");
        if (!QFile::rename(currentExe, newBakPath)) {
            showMessageDialog(&dialog, QString::fromUtf8("\u6062\u590d\u5931\u8d25"),
                QString::fromUtf8("\u65e0\u6cd5\u5907\u4efd\u5f53\u524d\u7a0b\u5e8f\u6587\u4ef6\u3002"));
            return;
        }
        if (!QFile::copy(bakExe, currentExe)) {
            QFile::rename(newBakPath, currentExe);
            showMessageDialog(&dialog, QString::fromUtf8("\u6062\u590d\u5931\u8d25"),
                QString::fromUtf8("\u65e0\u6cd5\u590d\u5236\u5907\u4efd\u6587\u4ef6\u3002"));
            return;
        }
        dialog.accept();
        if (showMessageDialog(this, QString::fromUtf8("\u6062\u590d\u6210\u529f"),
            QString::fromUtf8("\u5df2\u6062\u590d\u5230\u5907\u4efd\u7248\u672c\u3002\u662f\u5426\u7acb\u5373\u91cd\u542f\u8f6f\u4ef6\uff1f"), true)) {
            QProcess::startDetached(currentExe, QStringList());
            onExit();
        }
    });

    connect(updateBtn, &QPushButton::clicked, &dialog, [&]() {
        QString selectedFile = fileEdit->text().trimmed();
        if (selectedFile.isEmpty()) return;

        QString newExe = selectedFile;
        QString tempDir;

        // ZIP 文件处理
        if (selectedFile.toLower().endsWith(".zip")) {
            tempDir = QDir::temp().filePath("SuperGuardian_update_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
            QDir().mkpath(tempDir);
            QProcess proc;
            proc.start("powershell", QStringList() << "-NoProfile" << "-Command"
                << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(
                    QDir::toNativeSeparators(selectedFile).replace("'", "''"),
                    QDir::toNativeSeparators(tempDir).replace("'", "''")));
            proc.waitForFinished(60000);
            if (proc.exitCode() != 0) {
                showMessageDialog(&dialog, QString::fromUtf8("\u66f4\u65b0\u5931\u8d25"),
                    QString::fromUtf8("\u89e3\u538b ZIP \u6587\u4ef6\u5931\u8d25\u3002"));
                QDir(tempDir).removeRecursively();
                return;
            }
            newExe.clear();
            QDirIterator it(tempDir, QStringList() << "SuperGuardian.exe", QDir::Files, QDirIterator::Subdirectories);
            if (it.hasNext()) {
                newExe = it.next();
            } else {
                QDirIterator it2(tempDir, QStringList() << "*.exe", QDir::Files, QDirIterator::Subdirectories);
                QStringList exeFiles;
                while (it2.hasNext()) exeFiles << it2.next();
                if (exeFiles.size() == 1) newExe = exeFiles.first();
            }
            if (newExe.isEmpty()) {
                showMessageDialog(&dialog, QString::fromUtf8("\u66f4\u65b0\u5931\u8d25"),
                    QString::fromUtf8("ZIP \u5305\u4e2d\u672a\u627e\u5230 SuperGuardian.exe\u3002"));
                QDir(tempDir).removeRecursively();
                return;
            }
        }

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
            if (!tempDir.isEmpty()) QDir(tempDir).removeRecursively();
            return;
        }
        if (!QFile::copy(newExe, currentExe)) {
            QFile::rename(bakPath, currentExe);
            showMessageDialog(&dialog, QString::fromUtf8("\u66f4\u65b0\u5931\u8d25"),
                QString::fromUtf8("\u65e0\u6cd5\u590d\u5236\u65b0\u7248\u672c\u7a0b\u5e8f\u3002"));
            if (!tempDir.isEmpty()) QDir(tempDir).removeRecursively();
            return;
        }

        if (!tempDir.isEmpty()) QDir(tempDir).removeRecursively();

        // 清理旧备份，最多保留5个
        QDir bakDirObj(bakDir);
        QStringList entries = bakDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        while (entries.size() > 5) {
            QDir(bakDirObj.filePath(entries.takeFirst())).removeRecursively();
        }

        dialog.accept();

        showMessageDialog(this, QString::fromUtf8("\u66f4\u65b0\u6210\u529f"),
            QString::fromUtf8("\u7a0b\u5e8f\u5df2\u66f4\u65b0\u6210\u529f\u3002\u65e7\u7248\u672c\u5df2\u5907\u4efd\u5230 bak/%1/\n\u8f6f\u4ef6\u5c06\u81ea\u52a8\u91cd\u542f\u4ee5\u5e94\u7528\u66f4\u65b0\u3002").arg(timestamp));
        QProcess::startDetached(currentExe, QStringList());
        onExit();
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

// ---- 允许重复添加的程序白名单 ----

void SuperGuardian::showDuplicateWhitelistDialog() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0\u7684\u7a0b\u5e8f"));
    dlg.setMinimumSize(500, 360);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u4ee5\u4e0b\u7a0b\u5e8f\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0\u5230\u5217\u8868\u4e2d\uff1a\n\u7cfb\u7edf\u5185\u7f6e\u5de5\u5177\uff08\u5982 PowerShell\uff09\u9ed8\u8ba4\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0\u3002\n\u652f\u6301\u62d6\u653e\u7a0b\u5e8f\u6216\u5feb\u6377\u65b9\u5f0f\u5230\u5217\u8868\u4e2d\u6dfb\u52a0\u3002")));

    class DropListWidget : public QListWidget {
    public:
        std::function<void(const QString&)> onFileDropped;
        DropListWidget(QWidget* p = nullptr) : QListWidget(p) { setAcceptDrops(true); }
    protected:
        void dragEnterEvent(QDragEnterEvent* e) override { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
        void dragMoveEvent(QDragMoveEvent* e) override { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
        void dropEvent(QDropEvent* e) override {
            for (const QUrl& url : e->mimeData()->urls()) {
                QString file = url.toLocalFile();
                if (!file.isEmpty() && onFileDropped) onFileDropped(file);
            }
        }
    };

    DropListWidget* listWidget = new DropListWidget();
    for (const QString& path : duplicateWhitelist)
        listWidget->addItem(path);

    auto addFileToList = [&](const QString& rawPath) {
        QString file = rawPath;
        QFileInfo fi(file);
        if (fi.suffix().toLower() == "lnk") {
            file = resolveShortcut(file);
        }
        if (file.isEmpty()) return;
        for (int i = 0; i < listWidget->count(); i++) {
            if (listWidget->item(i)->text().compare(file, Qt::CaseInsensitive) == 0) return;
        }
        listWidget->addItem(file);
    };
    listWidget->onFileDropped = addFileToList;

    lay->addWidget(listWidget);

    QHBoxLayout* editLay = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton(QString::fromUtf8("\u6dfb\u52a0\u7a0b\u5e8f"));
    QPushButton* removeBtn = new QPushButton(QString::fromUtf8("\u5220\u9664\u9009\u4e2d"));
    editLay->addWidget(addBtn);
    editLay->addWidget(removeBtn);
    editLay->addStretch();
    lay->addLayout(editLay);

    QObject::connect(addBtn, &QPushButton::clicked, [&]() {
        QString file = QFileDialog::getOpenFileName(&dlg,
            QString::fromUtf8("\u9009\u62e9\u7a0b\u5e8f"), "", "Executable (*.exe);;Shortcut (*.lnk);;All Files (*)");
        if (!file.isEmpty()) addFileToList(file);
    });
    QObject::connect(removeBtn, &QPushButton::clicked, [&]() {
        int ci = listWidget->currentRow();
        if (ci >= 0) delete listWidget->takeItem(ci);
    });

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
    duplicateWhitelist.clear();
    for (int i = 0; i < listWidget->count(); i++)
        duplicateWhitelist.append(listWidget->item(i)->text());
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("duplicateWhitelist", duplicateWhitelist.join("|"));
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

// ---- 测试程序是否允许重复添加 ----

void SuperGuardian::testDuplicateAdd() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u6d4b\u8bd5\u7a0b\u5e8f\u662f\u5426\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0"));
    dlg.setFixedWidth(500);
    dlg.setMinimumHeight(160);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u8f93\u5165\u7a0b\u5e8f\u5b8c\u6574\u8def\u5f84\u6216\u540d\u79f0\uff0c\u6216\u62d6\u653e\u6587\u4ef6\u5230\u8f93\u5165\u6846\uff1a")));

    class DropLineEdit : public QLineEdit {
    public:
        DropLineEdit(QWidget* p = nullptr) : QLineEdit(p) { setAcceptDrops(true); }
    protected:
        void dragEnterEvent(QDragEnterEvent* e) override { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
        void dropEvent(QDropEvent* e) override {
            const QList<QUrl> urls = e->mimeData()->urls();
            if (!urls.isEmpty()) setText(urls.first().toLocalFile());
        }
    };

    DropLineEdit* input = new DropLineEdit();
    input->setPlaceholderText(QString::fromUtf8("\u7a0b\u5e8f\u8def\u5f84\u6216\u540d\u79f0\uff0c\u652f\u6301\u62d6\u653e"));
    lay->addWidget(input);

    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u68c0\u6d4b"));
    QPushButton* closeBtn = new QPushButton(QString::fromUtf8("\u5173\u95ed"));
    btnLay->addWidget(okBtn);
    btnLay->addWidget(closeBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);

    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, [&]() {
        QString path = input->text().trimmed();
        if (path.isEmpty()) {
            showMessageDialog(&dlg, QString::fromUtf8("\u63d0\u793a"), QString::fromUtf8("\u8bf7\u8f93\u5165\u7a0b\u5e8f\u8def\u5f84\u6216\u540d\u79f0\u3002"));
            return;
        }
        QString resolvedPath = path;
        QFileInfo fi(path);
        bool isSystemTool = false;
        if (!fi.exists()) {
            QString found = QStandardPaths::findExecutable(path);
            if (found.isEmpty()) {
                showMessageDialog(&dlg, QString::fromUtf8("\u68c0\u6d4b\u7ed3\u679c"),
                    QString::fromUtf8("\u7a0b\u5e8f\u4e0d\u5b58\u5728\uff0c\u4e5f\u4e0d\u662f\u7cfb\u7edf\u5185\u7f6e\u5de5\u5177\uff0c\u65e0\u6cd5\u6dfb\u52a0\u3002"));
                return;
            }
            resolvedPath = found;
            isSystemTool = true;
        }
        if (!isSystemTool) {
            QString nameOnly = QFileInfo(resolvedPath).fileName();
            QString found = QStandardPaths::findExecutable(nameOnly);
            if (!found.isEmpty() && QFileInfo(found).canonicalFilePath() == QFileInfo(resolvedPath).canonicalFilePath())
                isSystemTool = true;
        }
        QString targetPath = resolveShortcut(resolvedPath);
        QString displayName = QFileInfo(targetPath).fileName();
        if (isSystemTool) {
            showMessageDialog(&dlg, QString::fromUtf8("\u68c0\u6d4b\u7ed3\u679c"),
                QString::fromUtf8("\u300c%1\u300d\u662f\u7cfb\u7edf\u5185\u7f6e\u5de5\u5177\uff0c\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0\u3002").arg(displayName));
            return;
        }
        if (duplicateWhitelist.contains(resolvedPath, Qt::CaseInsensitive)) {
            showMessageDialog(&dlg, QString::fromUtf8("\u68c0\u6d4b\u7ed3\u679c"),
                QString::fromUtf8("\u300c%1\u300d\u5728\u91cd\u590d\u6dfb\u52a0\u767d\u540d\u5355\u4e2d\uff0c\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0\u3002").arg(displayName));
            return;
        }
        showMessageDialog(&dlg, QString::fromUtf8("\u68c0\u6d4b\u7ed3\u679c"),
            QString::fromUtf8("\u300c%1\u300d\u4e0d\u662f\u7cfb\u7edf\u5185\u7f6e\u5de5\u5177\uff0c\u4e5f\u4e0d\u5728\u767d\u540d\u5355\u4e2d\uff0c\u4e0d\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0\u3002").arg(displayName));
    });

    dlg.exec();
}
