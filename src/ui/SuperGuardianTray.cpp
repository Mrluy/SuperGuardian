#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <windows.h>

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
    if (self) {
        db.setValue(u"self_guard_manual_exit"_s, false);
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

    ConfigDatabase::instance().setValue(u"self_guard_manual_exit"_s, false);
    startWatchdogHelper();
    logRuntime(u"manual self-guard test triggered"_s);
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
    ConfigDatabase::instance().setValue(u"self_guard_manual_exit"_s, true);
    stopWatchdogHelper();
    logOperation(u"退出软件"_s);
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
    auto& db = ConfigDatabase::instance();
    QString current = db.value(u"theme"_s, u"light"_s).toString();
    QString next = (current == "dark") ? "light" : "dark";
    db.setValue(u"theme"_s, next);
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
