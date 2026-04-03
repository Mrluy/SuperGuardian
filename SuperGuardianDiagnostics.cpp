#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <windows.h>

using namespace Qt::Literals::StringLiterals;

void SuperGuardian::exportDiagnosticInfo() {
    QString timestamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    QString defaultName = u"SuperGuardian_Diagnostic_%1.txt"_s.arg(timestamp);
    QString filePath = QFileDialog::getSaveFileName(this,
        u"导出诊断信息"_s, defaultName, u"文本文件 (*.txt)"_s);
    if (filePath.isEmpty())
        return;

    QStringList lines;
    auto section = [&](const QString& title) {
        lines << QString();
        lines << u"═══════════════════════════════════════════════════════"_s;
        lines << u"  %1"_s.arg(title);
        lines << u"═══════════════════════════════════════════════════════"_s;
    };

    section(u"系统与应用信息"_s);
    lines << u"导出时间: %1"_s.arg(QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s));
    lines << u"应用版本: %1"_s.arg(QCoreApplication::applicationVersion());
    lines << u"Qt 版本: %1"_s.arg(qVersion());
    lines << u"操作系统: %1"_s.arg(QSysInfo::prettyProductName());
    lines << u"CPU 架构: %1"_s.arg(QSysInfo::currentCpuArchitecture());
    lines << u"应用路径: %1"_s.arg(QCoreApplication::applicationFilePath());
    lines << u"数据目录: %1"_s.arg(appDataDirPath());
    lines << u"PID: %1"_s.arg(QCoreApplication::applicationPid());

    {
        QByteArray probe = u"中文"_s.toUtf8();
        QString hex;
        for (int i = 0; i < probe.size(); ++i) {
            if (i)
                hex += u' ';
            hex += QString::asprintf("%02X", static_cast<unsigned char>(probe[i]));
        }
        lines << u"编码验证: \"%1\" → [%2] (预期: E4 B8 AD E6 96 87)"_s.arg(u"中文"_s, hex);
    }

    section(u"当前配置"_s);
    auto& db = ConfigDatabase::instance();
    QJsonObject allConfig = db.exportToJson();
    for (auto it = allConfig.begin(); it != allConfig.end(); ++it) {
        QString val = it.value().isString()
            ? it.value().toString()
            : QString::fromUtf8(QJsonDocument(QJsonArray{ it.value() }).toJson(QJsonDocument::Compact));
        if (val.length() > 200)
            val = val.left(200) + u"... (已截断)"_s;
        lines << u"  %1 = %2"_s.arg(it.key(), val);
    }

    section(u"守护项状态 (共 %1 项)"_s.arg(items.size()));
    for (int i = 0; i < items.size(); ++i) {
        const GuardItem& item = items[i];
        lines << u"--- [%1] %2 ---"_s.arg(i).arg(item.path);
        lines << u"  进程名: %1"_s.arg(item.processName);
        lines << u"  目标路径: %1"_s.arg(item.targetPath);
        if (!item.launchArgs.isEmpty())
            lines << u"  启动参数: %1"_s.arg(item.launchArgs);
        if (!item.note.isEmpty())
            lines << u"  备注: %1"_s.arg(item.note);
        lines << u"  守护中: %1"_s.arg(item.guarding ? u"是"_s : u"否"_s);
        lines << u"  已置顶: %1"_s.arg(item.pinned ? u"是"_s : u"否"_s);
        lines << u"  被守护次数: %1"_s.arg(item.restartCount);
        if (item.startTime.isValid())
            lines << u"  启动时间: %1"_s.arg(item.startTime.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        if (item.guardStartTime.isValid())
            lines << u"  守护开始: %1"_s.arg(item.guardStartTime.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        if (item.lastRestart.isValid())
            lines << u"  上次重启: %1"_s.arg(item.lastRestart.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        lines << u"  定时重启: %1 (%2条规则)"_s.arg(item.restartRulesActive ? u"启用"_s : u"停用"_s).arg(item.restartRules.size());
        lines << u"  定时运行: %1 (%2条规则)"_s.arg(item.scheduledRunEnabled ? u"启用"_s : u"停用"_s).arg(item.runRules.size());
        lines << u"  启动延时: %1秒"_s.arg(item.startDelaySecs);
        lines << u"  重试配置: 间隔%1秒, 最多%2次, 最长%3秒"_s
            .arg(item.retryConfig.retryIntervalSecs)
            .arg(item.retryConfig.maxRetries)
            .arg(item.retryConfig.maxDurationSecs);
        if (item.retryActive) {
            lines << u"  重试中: 当前第%1次, 开始于 %2"_s
                .arg(item.currentRetryCount)
                .arg(item.retryStartTime.toString(u"yyyy-MM-dd HH:mm:ss"_s));
        }
        int procCount = 0;
        bool running = isProcessRunning(item.processName, procCount);
        lines << u"  当前进程状态: %1 (实例数: %2)"_s.arg(running ? u"运行中"_s : u"未运行"_s).arg(procCount);
    }

    section(u"自我守护状态"_s);
    bool selfGuardEnabled = db.value(u"self_guard_enabled"_s, false).toBool();
    bool manualExit = db.value(u"self_guard_manual_exit"_s, false).toBool();
    int watchdogPid = db.value(u"watchdog_pid"_s, 0).toInt();
    lines << u"  自我守护: %1"_s.arg(selfGuardEnabled ? u"启用"_s : u"停用"_s);
    lines << u"  手动退出标记: %1"_s.arg(manualExit ? u"是"_s : u"否"_s);
    lines << u"  看门狗 PID: %1"_s.arg(watchdogPid > 0 ? QString::number(watchdogPid) : u"无"_s);
    if (watchdogPid > 0) {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>(watchdogPid));
        lines << u"  看门狗进程存活: %1"_s.arg(h ? u"是"_s : u"否"_s);
        if (h)
            CloseHandle(h);
    }

    auto appendLogs = [&](const QString& category, const QString& title, int limit) {
        section(u"最近%1 (最多%2条)"_s.arg(title).arg(limit));
        auto logs = LogDatabase::instance().queryLogs(category, limit);
        if (logs.isEmpty()) {
            lines << u"  (无记录)"_s;
            return;
        }
        for (const LogEntry& entry : logs) {
            QString program = entry.program.isEmpty() ? QString() : u" [%1]"_s.arg(entry.program);
            lines << u"  %1%2 %3"_s.arg(entry.timestamp.toString(u"MM-dd HH:mm:ss"_s), program, entry.message);
        }
    };
    appendLogs(u"runtime"_s, u"运行日志"_s, 50);
    appendLogs(u"operation"_s, u"操作日志"_s, 30);
    appendLogs(u"guard"_s, u"守护日志"_s, 30);
    appendLogs(u"scheduled_restart"_s, u"定时重启日志"_s, 20);
    appendLogs(u"scheduled_run"_s, u"定时运行日志"_s, 20);

    QFile f(filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("\xEF\xBB\xBF");
        f.write(lines.join(u"\r\n"_s).toUtf8());
        f.close();
        showMessageDialog(this, u"导出诊断信息"_s,
            u"诊断信息已导出到：\n%1"_s.arg(filePath));
        logOperation(u"导出诊断信息到 %1"_s.arg(filePath));
    } else {
        showMessageDialog(this, u"导出失败"_s,
            u"无法写入文件：%1"_s.arg(filePath));
    }
}
