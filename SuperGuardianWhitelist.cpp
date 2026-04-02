#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include <QtWidgets>

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
