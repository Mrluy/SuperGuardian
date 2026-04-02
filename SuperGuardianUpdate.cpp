#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include <QtWidgets>

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
            saveSettings();
            QProcess::startDetached(currentExe, QStringList() << "--restart");
            onExit();
        }
    });

    connect(updateBtn, &QPushButton::clicked, &dialog, [&]() {
        QString selectedFile = fileEdit->text().trimmed();
        if (selectedFile.isEmpty()) return;

        if (!showMessageDialog(&dialog, QString::fromUtf8("\u786e\u8ba4\u66f4\u65b0"),
            QString::fromUtf8("\u5373\u5c06\u4f7f\u7528\u4ee5\u4e0b\u6587\u4ef6\u8fdb\u884c\u66f4\u65b0\uff1a\n%1\n\n\u8bf7\u786e\u8ba4\u6587\u4ef6\u662f\u5426\u6b63\u786e\uff0c\u66f4\u65b0\u540e\u8f6f\u4ef6\u5c06\u81ea\u52a8\u91cd\u542f\u3002").arg(selectedFile), true))
            return;

        QString newExe = selectedFile;
        QString tempDir;

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

        QDir bakDirObj(bakDir);
        QStringList entries = bakDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        while (entries.size() > 5) {
            QDir(bakDirObj.filePath(entries.takeFirst())).removeRecursively();
        }

        dialog.accept();

        showMessageDialog(this, QString::fromUtf8("\u66f4\u65b0\u6210\u529f"),
            QString::fromUtf8("\u7a0b\u5e8f\u5df2\u66f4\u65b0\u6210\u529f\u3002\u65e7\u7248\u672c\u5df2\u5907\u4efd\u5230 bak/%1/\n\u8f6f\u4ef6\u5c06\u81ea\u52a8\u91cd\u542f\u4ee5\u5e94\u7528\u66f4\u65b0\u3002").arg(timestamp));
        saveSettings();
        QProcess::startDetached(currentExe, QStringList() << "--restart");
        onExit();
    });

    dialog.exec();
}
