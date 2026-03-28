#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"
#include <QtWidgets>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
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
    if (selfGuardAct) selfGuardAct->setChecked(self);
    if (autostartAct) autostartAct->setChecked(autoRun);
    if (self) {
        // Ensure watchdog is running even if QAction::toggled is not emitted by setChecked in some environments.
        QSettings ss(appSettingsFilePath(), QSettings::IniFormat);
        ss.setValue("self_guard_manual_exit", false);
        startWatchdogHelper();
    }
    syncSelfGuardListEntry(self);
}

static QIcon createSunIconForTray(int size = 18) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QColor color(220, 160, 0);
    qreal center = size / 2.0;
    qreal bodyR = size / 5.0;
    p.setPen(QPen(color, 1.5));
    for (int i = 0; i < 8; i++) {
        qreal angle = i * M_PI / 4.0;
        qreal inner = bodyR + 1.5;
        qreal outer = center - 1.0;
        p.drawLine(QPointF(center + inner * qCos(angle), center + inner * qSin(angle)),
                   QPointF(center + outer * qCos(angle), center + outer * qSin(angle)));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(center, center), bodyR, bodyR);
    return QIcon(pix);
}

static QIcon createMoonIconForTray(int size = 18) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QColor color(180, 200, 255);
    qreal center = size / 2.0;
    qreal r = size * 3.0 / 8.0;
    QPainterPath moon;
    moon.addEllipse(QPointF(center, center), r, r);
    QPainterPath cutout;
    cutout.addEllipse(QPointF(center + r * 0.45, center - r * 0.25), r * 0.75, r * 0.75);
    moon -= cutout;
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(moon);
    return QIcon(pix);
}

void SuperGuardian::applyTheme(const QString& theme) {
    applyAppTheme(theme);
    if (themeToggleBtn) {
        if (theme == "dark") {
            themeToggleBtn->setIcon(createMoonIconForTray());
            themeToggleBtn->setToolTip(QString::fromUtf8("暗色模式"));
        } else {
            themeToggleBtn->setIcon(createSunIconForTray());
            themeToggleBtn->setToolTip(QString::fromUtf8("浅色模式"));
        }
    }
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
        showMessageDialog(this, QString::fromUtf8("提示"), QString::fromUtf8("请先开启自我守护后再测试。"));
        return;
    }
    if (!showMessageDialog(this, QString::fromUtf8("测试自我守护"), QString::fromUtf8("将立即强制结束当前主进程，用于验证自我守护是否能自动重启。是否继续？"), true))
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
