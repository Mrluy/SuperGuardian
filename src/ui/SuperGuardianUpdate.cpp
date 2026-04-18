#include "SuperGuardian.h"
#include "ConfigDatabase.h"
#include "DialogHelpers.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>

namespace {

constexpr auto kLatestReleaseApi = "https://api.github.com/repos/Mrluy/SuperGuardian/releases/latest";

struct ReleaseAsset {
    QString name;
    QString downloadUrl;
};

struct ReleaseInfo {
    QString version;
    QString tagName;
    QString pageUrl;
    QList<ReleaseAsset> assets;
};

QString normalizedVersionString(QString version) {
    version = version.trimmed();
    while (version.startsWith(u'v', Qt::CaseInsensitive))
        version.remove(0, 1);
    return version;
}

QVector<int> versionParts(const QString& version) {
    QVector<int> parts;
    QRegularExpressionMatchIterator it =
        QRegularExpression(u"(\\d+)"_s).globalMatch(normalizedVersionString(version));
    while (it.hasNext())
        parts.append(it.next().captured(1).toInt());
    return parts;
}

int compareVersions(const QString& left, const QString& right) {
    const QVector<int> a = versionParts(left);
    const QVector<int> b = versionParts(right);
    const int count = qMax(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const int av = (i < a.size()) ? a[i] : 0;
        const int bv = (i < b.size()) ? b[i] : 0;
        if (av != bv)
            return (av < bv) ? -1 : 1;
    }
    return 0;
}

QString powerShellQuoted(const QString& value) {
    QString escaped = value;
    escaped.replace(u"'"_s, u"''"_s);
    return QString(u"'%1'"_s).arg(escaped);
}

bool runPowerShell(const QString& script, QByteArray* stdOut, QString* error,
    int timeoutMs, QProgressDialog* progress = nullptr) {
    if (stdOut)
        stdOut->clear();
    if (error)
        error->clear();

    QProcess process;
    process.start(u"powershell"_s, QStringList()
        << u"-NoProfile"_s
        << u"-ExecutionPolicy"_s
        << u"Bypass"_s
        << u"-Command"_s
        << script);
    if (!process.waitForStarted(5000)) {
        if (error)
            *error = u"无法启动 PowerShell。"_s;
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        qApp->processEvents();
        if (progress) {
            if (progress->wasCanceled()) {
                process.kill();
                process.waitForFinished();
                if (error)
                    *error = u"操作已取消。"_s;
                return false;
            }
        }

        if (process.waitForFinished(100))
            break;

        if (timer.elapsed() > timeoutMs) {
            process.kill();
            process.waitForFinished();
            if (error)
                *error = u"请求超时。"_s;
            return false;
        }
    }

    const QByteArray output = process.readAllStandardOutput();
    const QByteArray errOutput = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            QString errText = QString::fromUtf8(errOutput).trimmed();
            if (errText.isEmpty())
                errText = QString::fromUtf8(output).trimmed();
            *error = errText.isEmpty() ? u"PowerShell 执行失败。"_s : errText;
        }
        return false;
    }

    if (stdOut)
        *stdOut = output;
    return true;
}

bool fetchLatestReleaseInfo(ReleaseInfo* info, QString* error) {
    if (!info) {
        if (error)
            *error = u"内部错误：未提供版本输出对象。"_s;
        return false;
    }

    QByteArray payload;
    const QString userAgent = u"SuperGuardian/%1"_s.arg(QCoreApplication::applicationVersion());
    const QString script =
        u"$ProgressPreference='SilentlyContinue'; "
        u"[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        u"$headers=@{Accept='application/vnd.github+json';'User-Agent'=%1}; "
        u"(Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri %2).Content"_s.arg(
            powerShellQuoted(userAgent),
            powerShellQuoted(QString::fromLatin1(kLatestReleaseApi)));
    if (!runPowerShell(script, &payload, error, 10000))
        return false;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (!doc.isObject()) {
        if (error) {
            *error = parseError.error == QJsonParseError::NoError
                ? u"发布信息格式无效。"_s
                : u"发布信息解析失败：%1"_s.arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();
    info->tagName = root.value(u"tag_name"_s).toString();
    info->version = normalizedVersionString(info->tagName);
    if (info->version.isEmpty())
        info->version = normalizedVersionString(root.value(u"name"_s).toString());
    info->pageUrl = root.value(u"html_url"_s).toString();
    info->assets.clear();

    const QJsonArray assets = root.value(u"assets"_s).toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject assetObj = value.toObject();
        ReleaseAsset asset;
        asset.name = assetObj.value(u"name"_s).toString();
        asset.downloadUrl = assetObj.value(u"browser_download_url"_s).toString();
        if (!asset.name.isEmpty() && !asset.downloadUrl.isEmpty())
            info->assets.append(asset);
    }

    if (info->version.isEmpty()) {
        if (error)
            *error = u"未找到有效的 Release 版本号。"_s;
        return false;
    }
    return true;
}

ReleaseAsset pickPreferredAsset(const QList<ReleaseAsset>& assets) {
    ReleaseAsset best;
    int bestScore = 1000;
    for (const ReleaseAsset& asset : assets) {
        const QString lower = asset.name.toLower();
        int score = 1000;
        if (lower.contains(u"superguardian"_s) && lower.endsWith(u".zip"_s))
            score = 0;
        else if (lower.contains(u"superguardian"_s) && lower.endsWith(u".exe"_s))
            score = 1;
        else if (lower.endsWith(u".zip"_s))
            score = 2;
        else if (lower.endsWith(u".exe"_s))
            score = 3;

        if (score < bestScore) {
            bestScore = score;
            best = asset;
        }
    }
    return best;
}

bool downloadReleaseAsset(const ReleaseAsset& asset, const QString& filePath, QWidget* parent, QString* error) {
    QProgressDialog progress(parent);
    progress.setWindowTitle(u"在线更新"_s);
    progress.setLabelText(u"正在下载更新包..."_s);
    progress.setCancelButtonText(u"取消"_s);
    progress.setRange(0, 0);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    const QString userAgent = u"SuperGuardian/%1"_s.arg(QCoreApplication::applicationVersion());
    const QString script =
        u"$ProgressPreference='SilentlyContinue'; "
        u"$headers=@{'User-Agent'=%1}; "
        u"Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri %2 -OutFile %3"_s.arg(
            powerShellQuoted(userAgent),
            powerShellQuoted(asset.downloadUrl),
            powerShellQuoted(QDir::toNativeSeparators(filePath)));

    const bool ok = runPowerShell(script, nullptr, error, 600000, &progress);
    progress.close();
    if (!ok)
        return false;

    if (!QFileInfo::exists(filePath) || QFileInfo(filePath).size() <= 0) {
        if (error)
            *error = u"下载结果为空。"_s;
        return false;
    }
    return true;
}

} // namespace

bool SuperGuardian::installUpdatePackage(const QString& selectedFile, QWidget* dialogParent,
    bool confirmSelection, const QString& confirmMessage) {
    if (selectedFile.trimmed().isEmpty())
        return false;

    if (confirmSelection) {
        const QString message = confirmMessage.isEmpty()
            ? u"即将使用以下文件进行更新：\n%1\n\n请确认文件是否正确，更新后软件将自动重启。"_s
                .arg(QDir::toNativeSeparators(selectedFile))
            : confirmMessage;
        if (!showMessageDialog(dialogParent ? dialogParent : this, u"确认更新"_s, message, true))
            return false;
    }

    QString newExe = selectedFile;
    QString tempDir;

    if (selectedFile.toLower().endsWith(u".zip"_s)) {
        tempDir = QDir::temp().filePath(
            u"SuperGuardian_update_%1"_s.arg(QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s)));
        QDir().mkpath(tempDir);
        QProcess proc;
        proc.start(u"powershell"_s, QStringList()
            << u"-NoProfile"_s
            << u"-Command"_s
            << QString(
                u"Expand-Archive -Path '%1' -DestinationPath '%2' -Force"_s).arg(
                    QDir::toNativeSeparators(selectedFile).replace(u"'"_s, u"''"_s),
                    QDir::toNativeSeparators(tempDir).replace(u"'"_s, u"''"_s)));
        // 使用事件循环避免阻塞 UI 线程（阻塞会触发看门狗误判未响应）
        QElapsedTimer zipTimer;
        zipTimer.start();
        bool finished = false;
        while (proc.state() != QProcess::NotRunning) {
            qApp->processEvents();
            if (proc.waitForFinished(100)) { finished = true; break; }
            if (zipTimer.elapsed() > 60000) {
                proc.kill();
                proc.waitForFinished(1000);
                break;
            }
        }
        if (!finished) finished = (proc.state() == QProcess::NotRunning);
        if (!finished || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            showMessageDialog(dialogParent ? dialogParent : this, u"更新失败"_s,
                u"解压 ZIP 文件失败。"_s);
            QDir(tempDir).removeRecursively();
            return false;
        }

        newExe.clear();
        QDirIterator it(tempDir, QStringList() << u"SuperGuardian.exe"_s,
            QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            newExe = it.next();
        } else {
            QDirIterator it2(tempDir, QStringList() << u"*.exe"_s,
                QDir::Files, QDirIterator::Subdirectories);
            QStringList exeFiles;
            while (it2.hasNext())
                exeFiles << it2.next();
            if (exeFiles.size() == 1)
                newExe = exeFiles.first();
        }

        if (newExe.isEmpty()) {
            showMessageDialog(dialogParent ? dialogParent : this, u"更新失败"_s,
                u"ZIP 包中未找到 SuperGuardian.exe。"_s);
            QDir(tempDir).removeRecursively();
            return false;
        }
    }

    QString currentExe = QCoreApplication::applicationFilePath();
    QString appDir = QCoreApplication::applicationDirPath();
    QString bakDir = QDir(appDir).filePath(u"bak"_s);
    QString timestamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    QString bakSubDir = QDir(bakDir).filePath(timestamp);
    QDir().mkpath(bakSubDir);

    QString bakPath = QDir(bakSubDir).filePath(u"SuperGuardian.exe.bak"_s);
    if (!QFile::rename(currentExe, bakPath)) {
        showMessageDialog(dialogParent ? dialogParent : this, u"更新失败"_s,
            u"无法备份当前程序文件。"_s);
        if (!tempDir.isEmpty())
            QDir(tempDir).removeRecursively();
        return false;
    }
    if (!QFile::copy(newExe, currentExe)) {
        QFile::rename(bakPath, currentExe);
        showMessageDialog(dialogParent ? dialogParent : this, u"更新失败"_s,
            u"无法复制新版本程序。"_s);
        if (!tempDir.isEmpty())
            QDir(tempDir).removeRecursively();
        return false;
    }

    if (!tempDir.isEmpty())
        QDir(tempDir).removeRecursively();

    QDir bakDirObj(bakDir);
    QStringList entries = bakDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    while (entries.size() > 5)
        QDir(bakDirObj.filePath(entries.takeFirst())).removeRecursively();

    if (QDialog* dialog = qobject_cast<QDialog*>(dialogParent))
        dialog->accept();

    logOperation(u"软件更新"_s);
    showMessageDialog(this, u"更新成功"_s,
        u"程序已更新成功。旧版本已备份到 bak/%1/\n软件将自动重启以应用更新。"_s.arg(timestamp));
    saveSettings();
    QThread::msleep(500);
    appDir = QFileInfo(currentExe).absolutePath();
    qint64 pid = 0;
    bool started = QProcess::startDetached(currentExe, QStringList() << u"--restart"_s, appDir, &pid);
    if (!started) {
        QThread::msleep(1000);
        started = QProcess::startDetached(currentExe, QStringList() << u"--restart"_s, appDir, &pid);
    }
    onExit();
    return true;
}

void SuperGuardian::checkForOnlineUpdates(bool automatic, QWidget* uiParent, QLabel* statusLabel) {
    QWidget* parent = uiParent ? uiParent : this;
    if (statusLabel)
        statusLabel->setText(automatic ? u"正在检查更新..."_s : u"正在检查 GitHub Release..."_s);

    ReleaseInfo release;
    QString error;
    if (!fetchLatestReleaseInfo(&release, &error)) {
        if (statusLabel)
            statusLabel->setText(u"在线检查失败：%1"_s.arg(error));
        if (!automatic)
            showMessageDialog(parent, u"检查更新失败"_s, u"无法获取在线版本信息：\n%1"_s.arg(error));
        return;
    }

    const QString currentVersion = normalizedVersionString(QCoreApplication::applicationVersion());
    const QString latestVersion = normalizedVersionString(release.version);
    if (statusLabel) {
        statusLabel->setText(compareVersions(latestVersion, currentVersion) > 0
            ? u"发现新版本：v%1（当前 v%2）"_s.arg(latestVersion, currentVersion)
            : u"当前已是最新版本：v%1"_s.arg(currentVersion));
    }

    if (compareVersions(latestVersion, currentVersion) <= 0) {
        if (!automatic)
            showMessageDialog(parent, u"检查更新"_s,
                u"当前已是最新版本。\n\n当前版本：v%1"_s.arg(currentVersion));
        return;
    }

    const ReleaseAsset asset = pickPreferredAsset(release.assets);
    QMessageBox box(parent);
    box.setWindowTitle(u"发现新版本"_s);
    box.setIcon(QMessageBox::Information);
    box.setText(u"检测到新版本 v%1，当前版本为 v%2。"_s.arg(latestVersion, currentVersion));
    box.setInformativeText(!asset.downloadUrl.isEmpty()
        ? u"是否现在下载并更新？"_s
        : u"当前 Release 没有可直接安装的 .exe 或 .zip 资源，是否打开发布页面？"_s);
    QPushButton* downloadBtn = nullptr;
    if (!asset.downloadUrl.isEmpty())
        downloadBtn = box.addButton(u"下载并更新"_s, QMessageBox::AcceptRole);
    QPushButton* openBtn = box.addButton(u"打开发布页"_s, QMessageBox::ActionRole);
    QPushButton* laterBtn = box.addButton(u"稍后再说"_s, QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == laterBtn)
        return;

    if (box.clickedButton() == openBtn) {
        if (!release.pageUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(release.pageUrl));
        return;
    }

    if (box.clickedButton() != downloadBtn || asset.downloadUrl.isEmpty())
        return;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        showMessageDialog(parent, u"在线更新失败"_s, u"无法创建临时下载目录。"_s);
        return;
    }

    QString fileName = asset.name.trimmed();
    if (fileName.isEmpty())
        fileName = u"SuperGuardian_Update.zip"_s;
    const QString downloadPath = QDir(tempDir.path()).filePath(fileName);
    if (!downloadReleaseAsset(asset, downloadPath, parent, &error)) {
        if (statusLabel)
            statusLabel->setText(u"在线下载失败：%1"_s.arg(error));
        showMessageDialog(parent, u"在线更新失败"_s,
            u"下载更新包失败：\n%1"_s.arg(error));
        return;
    }

    installUpdatePackage(downloadPath, parent, false);
}

void SuperGuardian::showUpdateDialog() {
    QDialog dialog(this, kDialogFlags);
    dialog.setWindowTitle(u"软件更新"_s);
    dialog.setMinimumWidth(540);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* versionLabel = new QLabel(
        u"当前版本：v%1"_s.arg(normalizedVersionString(QCoreApplication::applicationVersion())));
    QFont versionFont = versionLabel->font();
    versionFont.setBold(true);
    versionLabel->setFont(versionFont);
    layout->addWidget(versionLabel);

    QGroupBox* onlineGroup = new QGroupBox(u"在线更新"_s);
    QVBoxLayout* onlineLayout = new QVBoxLayout(onlineGroup);
    onlineLayout->addWidget(new QLabel(
        u"从 GitHub Release 检查新版本。检测到新版本时，可直接下载并更新。"_s));

    QLabel* onlineStatusLabel = new QLabel(u"尚未检查在线更新。"_s);
    onlineStatusLabel->setWordWrap(true);
    onlineLayout->addWidget(onlineStatusLabel);

    QCheckBox* autoCheckBox = new QCheckBox(u"启动时自动检查更新"_s);
    autoCheckBox->setChecked(ConfigDatabase::instance().value(u"autoCheckUpdates"_s, false).toBool());
    onlineLayout->addWidget(autoCheckBox);

    QHBoxLayout* onlineBtnLayout = new QHBoxLayout();
    onlineBtnLayout->addStretch();
    QPushButton* checkNowBtn = new QPushButton(u"立即检查更新"_s);
    onlineBtnLayout->addWidget(checkNowBtn);
    onlineLayout->addLayout(onlineBtnLayout);
    layout->addWidget(onlineGroup);

    QObject::connect(autoCheckBox, &QCheckBox::toggled, this, [](bool checked) {
        ConfigDatabase::instance().setValue(u"autoCheckUpdates"_s, checked);
    });
    QObject::connect(checkNowBtn, &QPushButton::clicked, &dialog, [this, &dialog, onlineStatusLabel]() {
        checkForOnlineUpdates(false, &dialog, onlineStatusLabel);
    });

    QGroupBox* localGroup = new QGroupBox(u"本地更新"_s);
    QVBoxLayout* localLayout = new QVBoxLayout(localGroup);
    localLayout->addWidget(new QLabel(
        u"选择新版本的 SuperGuardian.exe 或 ZIP 压缩包进行更新。\n"
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
                    const QString path = url.toLocalFile().toLower();
                    if (path.endsWith(u".exe"_s) || path.endsWith(u".zip"_s)) {
                        e->acceptProposedAction();
                        return;
                    }
                }
            }
        }
        void dropEvent(QDropEvent* e) override {
            for (const QUrl& url : e->mimeData()->urls()) {
                const QString path = url.toLocalFile();
                const QString lower = path.toLower();
                if (lower.endsWith(u".exe"_s) || lower.endsWith(u".zip"_s)) {
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
    localLayout->addLayout(fileLayout);
    layout->addWidget(localGroup);

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
        const QString path = fileEdit->text().trimmed().toLower();
        updateBtn->setEnabled(!path.isEmpty() && (path.endsWith(u".exe"_s) || path.endsWith(u".zip"_s)));
    });

    connect(browseBtn, &QPushButton::clicked, &dialog, [&]() {
        const QString file = QFileDialog::getOpenFileName(&dialog,
            u"选择新版本程序"_s, QString(),
            u"Executable / ZIP (*.exe *.zip);;All Files (*)"_s);
        if (!file.isEmpty()) {
            const QString lower = file.toLower();
            if (lower.endsWith(u".exe"_s) || lower.endsWith(u".zip"_s))
                fileEdit->setText(file);
            else
                showMessageDialog(&dialog, u"提示"_s, u"仅支持 .exe 和 .zip 文件。"_s);
        }
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    connect(restoreBtn, &QPushButton::clicked, &dialog, [&]() {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString bakDir = QDir(appDir).filePath(u"bak"_s);
        QDir bakDirObj(bakDir);
        if (!bakDirObj.exists()) {
            showMessageDialog(&dialog, u"提示"_s, u"没有找到任何备份版本。"_s);
            return;
        }

        const QStringList entries =
            bakDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        if (entries.isEmpty()) {
            showMessageDialog(&dialog, u"提示"_s, u"没有找到任何备份版本。"_s);
            return;
        }

        QDialog restoreDlg(&dialog, kDialogFlags);
        restoreDlg.setWindowTitle(u"恢复旧版本"_s);
        restoreDlg.setMinimumSize(360, 300);
        QVBoxLayout* rlay = new QVBoxLayout(&restoreDlg);
        rlay->addWidget(new QLabel(u"选择要恢复的备份版本："_s));
        QListWidget* backupList = new QListWidget();
        for (const QString& entry : entries) {
            const QString bakExe =
                QDir(bakDirObj.filePath(entry)).filePath(u"SuperGuardian.exe.bak"_s);
            if (QFile::exists(bakExe)) {
                QString displayDate = entry;
                if (entry.length() == 15 && entry.contains(u"_")) {
                    const QDateTime dt = QDateTime::fromString(entry, u"yyyyMMdd_HHmmss"_s);
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
        rbtnLay->addWidget(restoreOkBtn);
        rbtnLay->addWidget(restoreCancelBtn);
        rlay->addLayout(rbtnLay);

        if (restoreDlg.exec() != QDialog::Accepted)
            return;

        QListWidgetItem* selected = backupList->currentItem();
        if (!selected)
            return;
        const QString selectedDir = selected->data(Qt::UserRole).toString();
        const QString bakExe =
            QDir(bakDirObj.filePath(selectedDir)).filePath(u"SuperGuardian.exe.bak"_s);
        const QString currentExe = QCoreApplication::applicationFilePath();

        const QString newBakDir =
            QDir(bakDir).filePath(QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s));
        QDir().mkpath(newBakDir);
        const QString newBakPath = QDir(newBakDir).filePath(u"SuperGuardian.exe.bak"_s);
        if (!QFile::rename(currentExe, newBakPath)) {
            showMessageDialog(&dialog, u"恢复失败"_s, u"无法备份当前程序文件。"_s);
            return;
        }
        if (!QFile::copy(bakExe, currentExe)) {
            QFile::rename(newBakPath, currentExe);
            showMessageDialog(&dialog, u"恢复失败"_s, u"无法复制备份文件。"_s);
            return;
        }

        dialog.accept();
        logOperation(u"恢复旧版本"_s);
        if (showMessageDialog(this, u"恢复成功"_s,
            u"已恢复到备份版本。是否立即重启软件？"_s, true)) {
            saveSettings();
            QThread::msleep(500);
            QString restartDir = QFileInfo(currentExe).absolutePath();
            qint64 pid = 0;
            bool started = QProcess::startDetached(
                currentExe, QStringList() << u"--restart"_s, restartDir, &pid);
            if (!started) {
                QThread::msleep(1000);
                started = QProcess::startDetached(
                    currentExe, QStringList() << u"--restart"_s, restartDir, &pid);
            }
            onExit();
        }
    });

    connect(updateBtn, &QPushButton::clicked, &dialog, [this, &dialog, fileEdit]() {
        const QString selectedFile = fileEdit->text().trimmed();
        if (selectedFile.isEmpty())
            return;

        installUpdatePackage(selectedFile, &dialog, true);
    });

    dialog.exec();
}
