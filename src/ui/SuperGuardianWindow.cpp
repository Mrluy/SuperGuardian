#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ThemeManager.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <windows.h>
#include <shlobj.h>

using namespace Qt::Literals::StringLiterals;

// ---- 窗口事件处理 ----

void SuperGuardian::toggleVisible() {
    if (isVisible() && !(windowState() & Qt::WindowMinimized)) {
        hide();
    } else {
        showNormal();
        raise();
        activateWindow();
        QTimer::singleShot(0, this, &SuperGuardian::resetColumnWidths);
    }
}

void SuperGuardian::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) toggleVisible();
}

void SuperGuardian::onExit() {
    if (m_exiting)
        return;

    m_exiting = true;
    ConfigDatabase::instance().setValue(u"self_guard_manual_exit"_s, true);
    stopWatchdogHelper();
    logOperation(u"退出软件"_s);

    if (tray)
        tray->hide();

    const auto topLevels = qApp->topLevelWidgets();
    for (QWidget* widget : topLevels) {
        if (!widget)
            continue;
        if (widget == this)
            continue;
        widget->close();
    }

    close();
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void SuperGuardian::closeEvent(QCloseEvent* event) {
    if (m_exiting) {
        QMainWindow::closeEvent(event);
        return;
    }

    if (tray) {
        hide();
        event->ignore();
    } else {
        QMainWindow::closeEvent(event);
    }
}

void SuperGuardian::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        if (!(windowState() & Qt::WindowMinimized) && isVisible()) {
            show();
            QTimer::singleShot(0, this, &SuperGuardian::resetColumnWidths);
        }
    }
    QMainWindow::changeEvent(event);
}

void SuperGuardian::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    distributeColumnWidths();
}

void SuperGuardian::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, &SuperGuardian::resetColumnWidths);
}

bool SuperGuardian::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    static const UINT WM_SG_SHOW = RegisterWindowMessageW(L"SuperGuardianShowMainWindow");
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_SG_SHOW) {
        showNormal();
        raise();
        activateWindow();
        QTimer::singleShot(0, this, &SuperGuardian::resetColumnWidths);
        if (result) *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

// ---- 工具函数 ----

void SuperGuardian::toggleTheme() {
    auto& db = ConfigDatabase::instance();
    QString current = db.value(u"theme"_s, u"light"_s).toString();
    QString next = (current == "dark") ? "light" : "dark";
    db.setValue(u"theme"_s, next);
    applyTheme(next);
}

void SuperGuardian::centerWindow() {
    QScreen* scr = screen();
    if (!scr) scr = QGuiApplication::screenAt(geometry().center());
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (!scr) return;
    QRect screenGeom = scr->availableGeometry();
    int x = screenGeom.x() + (screenGeom.width() - width()) / 2;
    int y = screenGeom.y() + (screenGeom.height() - height()) / 2;
    move(x, y);
    showNormal();
    raise();
    activateWindow();
}

void SuperGuardian::createDesktopShortcut() {
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString exeDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString shortcutPath = QDir::toNativeSeparators(QDir(desktop).filePath(u"超级守护.lnk"_s));

    const HRESULT initHr = CoInitialize(nullptr);
    const bool comReady = SUCCEEDED(initHr) || initHr == RPC_E_CHANGED_MODE;
    bool created = false;

    if (comReady) {
        IShellLinkW* shellLink = nullptr;
        const HRESULT createHr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
            IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
        if (SUCCEEDED(createHr) && shellLink) {
            shellLink->SetPath(reinterpret_cast<LPCWSTR>(exePath.utf16()));
            shellLink->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(exeDir.utf16()));
            shellLink->SetIconLocation(reinterpret_cast<LPCWSTR>(exePath.utf16()), 0);
            shellLink->SetDescription(L"超级守护");

            IPersistFile* persistFile = nullptr;
            const HRESULT queryHr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
            if (SUCCEEDED(queryHr) && persistFile) {
                const HRESULT saveHr = persistFile->Save(reinterpret_cast<LPCWSTR>(shortcutPath.utf16()), TRUE);
                created = SUCCEEDED(saveHr) && QFileInfo::exists(shortcutPath);
                persistFile->Release();
            }
            shellLink->Release();
        }
    }

    if (SUCCEEDED(initHr))
        CoUninitialize();

    showMessageDialog(this, u"桌面快捷方式"_s,
        created ? u"桌面快捷方式已创建：超级守护"_s
                : u"创建快捷方式失败，请检查权限。"_s);
}

void SuperGuardian::toggleAlwaysOnTop() {
    HWND hwnd = (HWND)winId();
    DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    bool isOnTop = (exStyle & WS_EX_TOPMOST) != 0;
    if (isOnTop) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (pinToggleBtn) pinToggleBtn->setChecked(false);
    } else {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (pinToggleBtn) pinToggleBtn->setChecked(true);
    }
    ConfigDatabase::instance().setValue(u"alwaysOnTop"_s, !isOnTop);
}

QString SuperGuardian::formatStartDelay(int secs) const {
    if (secs <= 0) return u"关闭"_s;
    return QString::number(secs) + u" 秒"_s;
}

QString SuperGuardian::formatDuration(qint64 secs) const {
    if (secs < 0) secs = 0;
    qint64 days = secs / 86400;
    qint64 hours = (secs % 86400) / 3600;
    qint64 mins = (secs % 3600) / 60;
    if (days > 0) return QString::number(days) + u"天"_s + QString::number(hours) + u"时"_s + QString::number(mins) + u"分"_s;
    if (hours > 0) return QString::number(hours) + u"时"_s + QString::number(mins) + u"分"_s;
    return QString::number(mins) + u"分"_s;
}

// ---- 关于对话框 ----

void SuperGuardian::showAboutDialog() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"关于 超级守护"_s);
    dlg.setFixedSize(420, 320);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QLabel* iconLabel = new QLabel();
    iconLabel->setPixmap(QIcon(u":/SuperGuardian/app.ico"_s).pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(iconLabel);
    QLabel* nameLabel = new QLabel(u"超级守护"_s);
    nameLabel->setAlignment(Qt::AlignCenter);
    QFont f = nameLabel->font();
    f.setPointSize(16);
    f.setBold(true);
    nameLabel->setFont(f);
    lay->addWidget(nameLabel);
    QLabel* verLabel = new QLabel(u"版本 v"_s + QCoreApplication::applicationVersion());
    verLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(verLabel);
    QLabel* descLabel = new QLabel(u"Windows 进程守护与定时管理工具"_s);
    descLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(descLabel);
    QLabel* openSourceLabel = new QLabel(u"本软件完全开源"_s);
    openSourceLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(openSourceLabel);
    QLabel* linkLabel = new QLabel(u"<a href=\"https://github.com/Mrluy/SuperGuardian/tree/master\">项目地址：https://github.com/Mrluy/SuperGuardian/tree/master</a>"_s);
    linkLabel->setAlignment(Qt::AlignCenter);
    linkLabel->setOpenExternalLinks(true);
    linkLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    lay->addWidget(linkLabel);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnLay->addWidget(okBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    dlg.exec();
}
