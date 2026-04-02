#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include <QtWidgets>
#include <windows.h>

// ---- 托盘选项、看门狗、主题 ----

void SuperGuardian::onSelfGuardToggled(bool on) {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("self_guard_enabled", on);
    if (on) s.setValue("self_guard_manual_exit", false);
    if (on) startWatchdogHelper(); else stopWatchdogHelper();
    syncSelfGuardListEntry(on);
}

void SuperGuardian::onAutostartToggled(bool on) {
    setAutostart(on);
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("autostart", on);
}

void SuperGuardian::applySavedTrayOptions() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    bool self = s.value("self_guard_enabled", false).toBool();
    bool autoRun = s.value("autostart", false).toBool();
    bool emailOn = s.value("emailEnabled", false).toBool();
    bool minToTray = s.value("minimizeToTray", false).toBool();
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
    if (self) {
        // Ensure watchdog is running even if QAction::toggled is not emitted by setChecked in some environments.
        QSettings ss(appSettingsFilePath(), QSettings::IniFormat);
        ss.setValue("self_guard_manual_exit", false);
        startWatchdogHelper();
    }
    syncSelfGuardListEntry(self);
}

void SuperGuardian::applyTheme(const QString& theme) {
    applyAppTheme(theme);
    if (themeToggleBtn) {
        if (theme == "dark") {
            themeToggleBtn->setIcon(QIcon(":/SuperGuardian/light.png"));
            themeToggleBtn->setToolTip(u"切换到浅色模式"_s);
        } else {
            themeToggleBtn->setIcon(QIcon(":/SuperGuardian/dark.png"));
            themeToggleBtn->setToolTip(u"切换到暗色模式"_s);
        }
    }
    if (pinToggleBtn) {
        if (theme == "dark")
            pinToggleBtn->setIcon(QIcon(":/SuperGuardian/top_light.png"));
        else
            pinToggleBtn->setIcon(QIcon(":/SuperGuardian/top_dark.png"));
    }
    rebuildTableFromItems();
}

void SuperGuardian::syncSelfGuardListEntry(bool enabled) {
    Q_UNUSED(enabled);

    // 不再在列表中显示 SuperGuardianWatchdog.exe。
    // 这里只做一次性清理：移除旧版本残留的 internalSelfGuard 行。
    for (int i = items.size() - 1; i >= 0; --i) {
        if (!items[i].internalSelfGuard) continue;
        int row = findRowByPath(items[i].path);
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

    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("self_guard_manual_exit", false);
    s.sync();
    startWatchdogHelper();
    appendWatchdogLog("manual self-guard test triggered");
    ::TerminateProcess(GetCurrentProcess(), 99);
}

void SuperGuardian::startWatchdogHelper() {
    // start self as watchdog with --watchdog <pid> <exe>
    QString exe = QCoreApplication::applicationFilePath();
    DWORD pid = GetCurrentProcessId();
    // ensure previous helper is not alive
    stopWatchdogHelper();
    qint64 newPid = 0;
    QStringList args{ "--watchdog", QString::number(pid), exe };
    bool ok = QProcess::startDetached(exe, args, QFileInfo(exe).absolutePath(), &newPid);
    if (ok && newPid > 0) {
        QSettings s(appSettingsFilePath(), QSettings::IniFormat);
        s.setValue("watchdog_pid", (int)newPid);
        appendWatchdogLog(QString("main started watchdog pid=%1 exe=%2").arg((int)newPid).arg(exe));
    } else {
        appendWatchdogLog(QString("main failed to start watchdog exe=%1").arg(exe), GetLastError());
    }
}

void SuperGuardian::stopWatchdogHelper() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    int pid = s.value("watchdog_pid", 0).toInt();
    if (pid>0) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
        s.remove("watchdog_pid");
    }
}

// ---- 窗口事件处理 ----

void SuperGuardian::toggleVisible() {
    if (isVisible() && !(windowState() & Qt::WindowMinimized)) {
        hide();
    } else {
        showNormal();
        raise();
        activateWindow();
    }
}

void SuperGuardian::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) toggleVisible();
}

void SuperGuardian::onExit() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("self_guard_manual_exit", true);
    stopWatchdogHelper();
    qApp->quit();
}

void SuperGuardian::closeEvent(QCloseEvent* event) {
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
    static bool firstShow = true;
    if (firstShow) {
        firstShow = false;
        QTimer::singleShot(0, this, &SuperGuardian::distributeColumnWidths);
    }
}

bool SuperGuardian::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    static const UINT WM_SG_SHOW = RegisterWindowMessageW(L"SuperGuardianShowMainWindow");
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_SG_SHOW) {
        showNormal();
        raise();
        activateWindow();
        if (result) *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void SuperGuardian::toggleTheme() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QString current = s.value("theme", "light").toString();
    QString next = (current == "dark") ? "light" : "dark";
    s.setValue("theme", next);
    applyTheme(next);
}

void SuperGuardian::centerWindow() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect screenGeom = screen->availableGeometry();
    int x = screenGeom.x() + (screenGeom.width() - width()) / 2;
    int y = screenGeom.y() + (screenGeom.height() - height()) / 2;
    move(x, y);
    showNormal();
    raise();
    activateWindow();
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
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("alwaysOnTop", !isOnTop);
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
