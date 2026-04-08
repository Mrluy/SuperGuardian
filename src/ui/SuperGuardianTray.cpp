#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QPainter>
#include <QFileInfo>
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>

// ---- 图标生成 ----

QIcon SuperGuardian::makeToolbarIcon(const QString& iconName, bool active, const QString& theme) const {
    // dark主题使用light文件夹图标，light主题使用dark文件夹图标
    QString iconDir = (theme == "dark") ? u"light"_s : u"dark"_s;

    // 全局守护特殊处理：激活时使用 app.ico
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

    // 工具栏按钮
    if (selfGuardBtn) { selfGuardBtn->setIcon(keepaliveIcon); selfGuardBtn->setChecked(selfGuardOn); }
    if (autostartBtn) { autostartBtn->setIcon(startupIcon); autostartBtn->setChecked(autostartOn); }
    if (minimizeToTrayBtn) { minimizeToTrayBtn->setIcon(trayifyIcon); minimizeToTrayBtn->setChecked(minTrayOn); }
    if (globalGuardBtn) { globalGuardBtn->setIcon(watcherIcon); globalGuardBtn->setChecked(gGuardOn); }
    if (globalRestartBtn) { globalRestartBtn->setIcon(scheduleIcon); globalRestartBtn->setChecked(gRestartOn); }
    if (globalRunBtn) { globalRunBtn->setIcon(runnerIcon); globalRunBtn->setChecked(gRunOn); }
    if (globalEmailBtn) { globalEmailBtn->setIcon(mailIcon); globalEmailBtn->setChecked(gEmailOn); }

    // 菜单项图标（选项卡 + 功能卡）
    if (selfGuardAct) selfGuardAct->setIcon(keepaliveIcon);
    if (autostartAct) autostartAct->setIcon(startupIcon);
    if (minimizeToTrayAct) minimizeToTrayAct->setIcon(trayifyIcon);
    if (globalGuardAct) globalGuardAct->setIcon(watcherIcon);
    if (globalRestartAct) globalRestartAct->setIcon(scheduleIcon);
    if (globalRunAct) globalRunAct->setIcon(runnerIcon);
    if (emailEnabledAct) emailEnabledAct->setIcon(mailIcon);

    // 托盘菜单图标
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

    // 加载全局功能状态
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
    // dark主题使用light文件夹图标，light主题使用dark文件夹图标
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

    // 不再在列表中显示 SuperGuardianWatchdog.exe。
    // 这里只做一次性清理：移除旧版本残留的 internalSelfGuard 行。
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
