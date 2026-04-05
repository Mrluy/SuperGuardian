#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include <QtWidgets>
#include <QMimeData>

using namespace Qt::Literals::StringLiterals;

// ---- 测试程序是否允许重复添加 ----

void SuperGuardian::testDuplicateAdd() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"测试程序是否允许重复添加"_s);
    dlg.setFixedWidth(500);
    dlg.setMinimumHeight(160);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"输入程序完整路径或名称，或拖放文件到输入框："_s));

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
    input->setPlaceholderText(u"程序路径或名称，支持拖放和快捷方式"_s);
    lay->addWidget(input);

    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"检测"_s);
    QPushButton* closeBtn = new QPushButton(u"关闭"_s);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(closeBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);

    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, [&]() {
        QString path = input->text().trimmed();
        if (path.isEmpty()) {
            showMessageDialog(&dlg, u"提示"_s, u"请输入程序路径或名称。"_s);
            return;
        }
        QString resolvedPath = path;
        QFileInfo fi(path);
        bool isSystemTool = false;
        if (fi.exists() && fi.suffix().toLower() == "lnk") {
            QString lnkTarget = resolveShortcut(path);
            if (lnkTarget.isEmpty() || lnkTarget.compare(path, Qt::CaseInsensitive) == 0) {
                showMessageDialog(&dlg, u"检测结果"_s, u"无法解析快捷方式的目标程序。"_s);
                return;
            }
            resolvedPath = lnkTarget;
            fi = QFileInfo(resolvedPath);
        }
        if (!fi.exists()) {
            QString found = QStandardPaths::findExecutable(resolvedPath);
            if (found.isEmpty()) {
                showMessageDialog(&dlg, u"检测结果"_s, u"程序不存在，也不是系统内置工具，无法添加。"_s);
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
            showMessageDialog(&dlg, u"检测结果"_s, u"「%1」是系统内置工具，允许重复添加。"_s.arg(displayName));
            return;
        }
        if (duplicateWhitelist.contains(targetPath, Qt::CaseInsensitive) ||
            duplicateWhitelist.contains(resolvedPath, Qt::CaseInsensitive)) {
            showMessageDialog(&dlg, u"检测结果"_s, u"「%1」在重复添加白名单中，允许重复添加。"_s.arg(displayName));
            return;
        }
        showMessageDialog(&dlg, u"检测结果"_s, u"「%1」不是系统内置工具，也不在白名单中，不允许重复添加。"_s.arg(displayName));
    });

    dlg.exec();
}
