#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QThread>

// ---- 本地更新 ----

void SuperGuardian::showUpdateDialog() {
    QDialog dialog(this, kDialogFlags);
    dialog.setWindowTitle(u"软件更新"_s);
    dialog.setMinimumWidth(500);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(u"选择新版本的 SuperGuardian.exe 或 ZIP 压缩包进行更新。\n"
        u"支持拖放文件到输入框。\n"
        u"更新时将自动备份旧版本到 bak 文件夹，最多保留 5 个旧版本。"_s));

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
    fileEdit->setPlaceholderText(u"选择或拖放新版本 .exe 或 .zip 文件"_s);
    QPushButton* browseBtn = new QPushButton(u"浏览"_s);
    fileLayout->addWidget(fileEdit);
    fileLayout->addWidget(browseBtn);
    layout->addLayout(fileLayout);

    layout->addStretch();

    QHBoxLayout* restoreLayout = new QHBoxLayout();
    restoreLayout->addStretch();
    QPushButton* restoreBtn = new QPushButton(u"恢复旧版本"_s);
    restoreLayout->addWidget(restoreBtn);
    layout->addLayout(restoreLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* updateBtn = new QPushButton(u"开始更新"_s);
    updateBtn->setEnabled(false);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    btnLayout->addWidget(updateBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(fileEdit, &QLineEdit::textChanged, &dialog, [updateBtn, fileEdit]() {
        QString path = fileEdit->text().trimmed().toLower();
        updateBtn->setEnabled(!path.isEmpty() && (path.endsWith(".exe") || path.endsWith(".zip")));
    });

    connect(browseBtn, &QPushButton::clicked, &dialog, [&]() {
        QString file = QFileDialog::getOpenFileName(&dialog,
            u"选择新版本程序"_s, "",
            u"Executable / ZIP (*.exe *.zip);;All Files (*)"_s);
        if (!file.isEmpty()) {
            QString lower = file.toLower();
            if (lower.endsWith(".exe") || lower.endsWith(".zip"))
                fileEdit->setText(file);
            else
                showMessageDialog(&dialog, u"提示"_s,
                    u"仅支持 .exe 和 .zip 文件。"_s);
        }
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    // 恢复旧版本
    connect(restoreBtn, &QPushButton::clicked, &dialog, [&]() {
        QString appDir = QCoreApplication::applicationDirPath();
        QString bakDir = QDir(appDir).filePath("bak");
        QDir bakDirObj(bakDir);
        if (!bakDirObj.exists()) {
            showMessageDialog(&dialog, u"提示"_s,
                u"没有找到任何备份版本。"_s);
            return;
        }
        QStringList entries = bakDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        if (entries.isEmpty()) {
            showMessageDialog(&dialog, u"提示"_s,
                u"没有找到任何备份版本。"_s);
            return;
        }

        QDialog restoreDlg(&dialog, kDialogFlags);
        restoreDlg.setWindowTitle(u"恢复旧版本"_s);
        restoreDlg.setMinimumSize(360, 300);
        QVBoxLayout* rlay = new QVBoxLayout(&restoreDlg);
        rlay->addWidget(new QLabel(u"选择要恢复的备份版本："_s));
        QListWidget* backupList = new QListWidget();
        for (const QString& entry : entries) {
            QString bakExe = QDir(bakDirObj.filePath(entry)).filePath("SuperGuardian.exe.bak");
            if (QFile::exists(bakExe)) {
                QString displayDate = entry;
                if (entry.length() == 15 && entry.contains('_')) {
                    QDateTime dt = QDateTime::fromString(entry, "yyyyMMdd_HHmmss");
                    if (dt.isValid())
                        displayDate = dt.toString(u"yyyy年M月d日 HH:mm:ss"_s);
                }
                QListWidgetItem* item = new QListWidgetItem(displayDate);
                item->setData(Qt::UserRole, entry);
                backupList->addItem(item);
            }
        }
        rlay->addWidget(backupList);
        QHBoxLayout* rbtnLay = new QHBoxLayout();
        rbtnLay->addStretch();
        QPushButton* restoreOkBtn = new QPushButton(u"恢复"_s);
        QPushButton* restoreCancelBtn = new QPushButton(u"取消"_s);
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
            showMessageDialog(&dialog, u"恢复失败"_s,
                u"无法备份当前程序文件。"_s);
            return;
        }
        if (!QFile::copy(bakExe, currentExe)) {
            QFile::rename(newBakPath, currentExe);
            showMessageDialog(&dialog, u"恢复失败"_s,
                u"无法复制备份文件。"_s);
            return;
        }
        dialog.accept();
        logOperation(u"恢复旧版本"_s);
        if (showMessageDialog(this, u"恢复成功"_s,
            u"已恢复到备份版本。是否立即重启软件？"_s, true)) {
            saveSettings();
            QThread::msleep(500);
            QString appDir = QFileInfo(currentExe).absolutePath();
            qint64 pid = 0;
            bool started = QProcess::startDetached(currentExe, QStringList() << "--restart", appDir, &pid);
            if (!started) {
                QThread::msleep(1000);
                started = QProcess::startDetached(currentExe, QStringList() << "--restart", appDir, &pid);
            }
            onExit();
        }
    });

    connect(updateBtn, &QPushButton::clicked, &dialog, [&]() {
        QString selectedFile = fileEdit->text().trimmed();
        if (selectedFile.isEmpty()) return;

        if (!showMessageDialog(&dialog, u"确认更新"_s,
            u"即将使用以下文件进行更新：\n%1\n\n请确认文件是否正确，更新后软件将自动重启。"_s.arg(selectedFile), true))
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
            bool finished = proc.waitForFinished(60000);
            if (!finished || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
                showMessageDialog(&dialog, u"更新失败"_s,
                    u"解压 ZIP 文件失败。"_s);
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
                showMessageDialog(&dialog, u"更新失败"_s,
                    u"ZIP 包中未找到 SuperGuardian.exe。"_s);
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
            showMessageDialog(&dialog, u"更新失败"_s,
                u"无法备份当前程序文件。"_s);
            if (!tempDir.isEmpty()) QDir(tempDir).removeRecursively();
            return;
        }
        if (!QFile::copy(newExe, currentExe)) {
            QFile::rename(bakPath, currentExe);
            showMessageDialog(&dialog, u"更新失败"_s,
                u"无法复制新版本程序。"_s);
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

        logOperation(u"软件更新"_s);
        showMessageDialog(this, u"更新成功"_s,
            u"程序已更新成功。旧版本已备份到 bak/%1/\n软件将自动重启以应用更新。"_s.arg(timestamp));
        saveSettings();
        QThread::msleep(500);
        appDir = QFileInfo(currentExe).absolutePath();
        qint64 pid = 0;
        bool started = QProcess::startDetached(currentExe, QStringList() << "--restart", appDir, &pid);
        if (!started) {
            QThread::msleep(1000);
            started = QProcess::startDetached(currentExe, QStringList() << "--restart", appDir, &pid);
        }
        onExit();
    });

    dialog.exec();
}
