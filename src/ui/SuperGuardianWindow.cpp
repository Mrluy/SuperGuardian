#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QPainter>
#include <QFileInfo>
#include <objbase.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <windows.h>

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

// ---- 图标生成 ----

QIcon SuperGuardian::makeToolbarIcon(const QString& iconName, bool active, const QString& theme) const {
    QString iconDir = (theme == "dark") ? u"light"_s : u"dark"_s;

    QString resPath;
    if (iconName == u"watcher"_s && active)
        resPath = u":/SuperGuardian/app.ico"_s;
    else
        resPath = u":/SuperGuardian/%1/%2.png"_s.arg(iconDir, iconName);

    const int canvasSize = 20;
    const int iconSize = 18;
    QPixmap pix(canvasSize, canvasSize);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    if (active) {
        const QColor bgColor = (theme == "dark") ? QColor(0x20, 0x47, 0x6e) : QColor(0xdd, 0xed, 0xfe);
        p.setBrush(bgColor);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(0, 0, canvasSize, canvasSize, 5, 5);
    }

    const QPixmap srcPix = QIcon(resPath).pixmap(iconSize, iconSize);
    const int offset = (canvasSize - iconSize) / 2;
    p.drawPixmap(offset, offset,
        srcPix.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    p.end();

    return QIcon(pix);
}

void SuperGuardian::updateToolbarIcons() {
    auto& db = ConfigDatabase::instance();
    QString theme = db.value(u"theme"_s, u"light"_s).toString();

    bool selfGuardOn = selfGuardAct && selfGuardAct->isChecked();
    bool autostartOn = autostartAct && autostartAct->isChecked();
    bool minTrayOn = minimizeToTrayAct && minimizeToTrayAct->isChecked();
    bool gGuardOn = globalGuardAct && globalGuardAct->isChecked();
    bool gRestartOn = globalRestartAct && globalRestartAct->isChecked();
    bool gRunOn = globalRunAct && globalRunAct->isChecked();
    bool gEmailOn = emailEnabledAct && emailEnabledAct->isChecked();

    QIcon keepaliveIcon = makeToolbarIcon(u"keepalive"_s, selfGuardOn, theme);
    QIcon startupIcon = makeToolbarIcon(u"startup"_s, autostartOn, theme);
    QIcon trayifyIcon = makeToolbarIcon(u"trayify"_s, minTrayOn, theme);
    QIcon watcherIcon = makeToolbarIcon(u"watcher"_s, gGuardOn, theme);
    QIcon scheduleIcon = makeToolbarIcon(u"schedule"_s, gRestartOn, theme);
    QIcon runnerIcon = makeToolbarIcon(u"runner"_s, gRunOn, theme);
    QIcon mailIcon = makeToolbarIcon(u"mail"_s, gEmailOn, theme);

    if (selfGuardBtn) { selfGuardBtn->setIcon(keepaliveIcon); selfGuardBtn->setChecked(selfGuardOn); }
    if (autostartBtn) { autostartBtn->setIcon(startupIcon); autostartBtn->setChecked(autostartOn); }
    if (minimizeToTrayBtn) { minimizeToTrayBtn->setIcon(trayifyIcon); minimizeToTrayBtn->setChecked(minTrayOn); }
    if (globalGuardBtn) { globalGuardBtn->setIcon(watcherIcon); globalGuardBtn->setChecked(gGuardOn); }
    if (globalRestartBtn) { globalRestartBtn->setIcon(scheduleIcon); globalRestartBtn->setChecked(gRestartOn); }
    if (globalRunBtn) { globalRunBtn->setIcon(runnerIcon); globalRunBtn->setChecked(gRunOn); }
    if (globalEmailBtn) { globalEmailBtn->setIcon(mailIcon); globalEmailBtn->setChecked(gEmailOn); }

    if (selfGuardAct) selfGuardAct->setIcon(keepaliveIcon);
    if (autostartAct) autostartAct->setIcon(startupIcon);
    if (minimizeToTrayAct) minimizeToTrayAct->setIcon(trayifyIcon);
    if (globalGuardAct) globalGuardAct->setIcon(watcherIcon);
    if (globalRestartAct) globalRestartAct->setIcon(scheduleIcon);
    if (globalRunAct) globalRunAct->setIcon(runnerIcon);
    if (emailEnabledAct) emailEnabledAct->setIcon(mailIcon);

    if (trayEmailAct) trayEmailAct->setIcon(mailIcon);
}

// ---- 托盘选项、看门狗、主题 ----

void SuperGuardian::onSelfGuardToggled(bool on) {
    auto& db = ConfigDatabase::instance();
    db.setValue(u"self_guard_enabled"_s, on);
    if (on) db.setValue(u"self_guard_manual_exit"_s, false);
    if (on) startWatchdogHelper(); else stopWatchdogHelper();
    syncSelfGuardListEntry(on);
    logOperation(on ? u"开启自我守护"_s : u"关闭自我守护"_s);
}

void SuperGuardian::onAutostartToggled(bool on) {
    setAutostart(on);
    ConfigDatabase::instance().setValue(u"autostart"_s, on);
    logOperation(on ? u"开启开机自启"_s : u"关闭开机自启"_s);
}

void SuperGuardian::applySavedTrayOptions() {
    auto& db = ConfigDatabase::instance();
    bool self = db.value(u"self_guard_enabled"_s, false).toBool();
    bool autoRun = db.value(u"autostart"_s, false).toBool();
    bool emailOn = db.value(u"emailEnabled"_s, false).toBool();
    bool minToTray = db.value(u"minimizeToTray"_s, false).toBool();
    if (selfGuardAct) selfGuardAct->setChecked(self);
    if (autostartAct) autostartAct->setChecked(autoRun);
    if (trayEmailAct) {
        trayEmailAct->blockSignals(true);
        trayEmailAct->setChecked(emailOn);
        trayEmailAct->blockSignals(false);
    }
    if (minimizeToTrayAct) {
        minimizeToTrayAct->blockSignals(true);
        minimizeToTrayAct->setChecked(minToTray);
        minimizeToTrayAct->blockSignals(false);
    }

    bool gGuard = db.value(u"globalGuardEnabled"_s, false).toBool();
    bool gRestart = db.value(u"globalRestartEnabled"_s, false).toBool();
    bool gRun = db.value(u"globalRunEnabled"_s, false).toBool();
    if (globalGuardAct) { globalGuardAct->blockSignals(true); globalGuardAct->setChecked(gGuard); globalGuardAct->blockSignals(false); }
    if (globalRestartAct) { globalRestartAct->blockSignals(true); globalRestartAct->setChecked(gRestart); globalRestartAct->blockSignals(false); }
    if (globalRunAct) { globalRunAct->blockSignals(true); globalRunAct->setChecked(gRun); globalRunAct->blockSignals(false); }

    if (self) {
        db.setValue(u"self_guard_manual_exit"_s, false);
        startWatchdogHelper();
    }
    syncSelfGuardListEntry(self);
    updateToolbarIcons();
}

void SuperGuardian::applyTheme(const QString& theme) {
    applyAppTheme(theme);
    QString iconDir = (theme == "dark") ? u"light"_s : u"dark"_s;
    if (themeToggleBtn) {
        themeToggleBtn->setIcon(QIcon(u":/SuperGuardian/%1/theme.png"_s.arg(iconDir)));
        themeToggleBtn->setToolTip(theme == "dark" ? u"切换到浅色模式"_s : u"切换到暗色模式"_s);
    }
    if (pinToggleBtn) {
        pinToggleBtn->setIcon(QIcon(u":/SuperGuardian/%1/top.png"_s.arg(iconDir)));
    }
    updateToolbarIcons();
    rebuildTableFromItems();
}

void SuperGuardian::syncSelfGuardListEntry(bool enabled) {
    Q_UNUSED(enabled);

    for (int i = items.size() - 1; i >= 0; --i) {
        if (!items[i].internalSelfGuard) continue;
        int row = findRowById(items[i].id);
        if (row >= 0) tableWidget->removeRow(row);
        items.removeAt(i);
    }
}

void SuperGuardian::runSelfGuardTest() {
    if (!selfGuardAct || !selfGuardAct->isChecked()) {
        showMessageDialog(this, u"提示"_s, u"请先开启自我守护后再测试。"_s);
        return;
    }
    if (!showMessageDialog(this, u"测试自我守护"_s, u"将立即强制结束当前主进程，用于验证自我守护是否能自动重启。是否继续？"_s, true))
        return;

    ConfigDatabase::instance().setValue(u"self_guard_manual_exit"_s, false);
    startWatchdogHelper();
    logOperation(u"测试自我守护"_s);
    logRuntime(u"manual self-guard test triggered"_s);
    ::TerminateProcess(GetCurrentProcess(), 99);
}

void SuperGuardian::startWatchdogHelper() {
    QString exe = QCoreApplication::applicationFilePath();
    DWORD pid = GetCurrentProcessId();
    stopWatchdogHelper();
    qint64 newPid = 0;
    QStringList args{ "--watchdog", QString::number(pid), exe };
    bool ok = QProcess::startDetached(exe, args, QFileInfo(exe).absolutePath(), &newPid);
    if (ok && newPid > 0) {
        ConfigDatabase::instance().setValue(u"watchdog_pid"_s, (int)newPid);
        logRuntime(QString("main started watchdog pid=%1 exe=%2").arg((int)newPid).arg(exe));
    } else {
        logRuntime(QString("main failed to start watchdog exe=%1 (err=%2)").arg(exe).arg(GetLastError()));
    }
}

void SuperGuardian::stopWatchdogHelper() {
    auto& db = ConfigDatabase::instance();
    int pid = db.value(u"watchdog_pid"_s, 0).toInt();
    if (pid>0) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
        db.remove(u"watchdog_pid"_s);
    }
}
