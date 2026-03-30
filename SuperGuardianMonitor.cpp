#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "AppStorage.h"
#include <QtWidgets>
#include <QThread>

// ---- 进程监控主循环 ----

void SuperGuardian::checkProcesses() {
    QDateTime now = QDateTime::currentDateTime();
    for (int row = 0; row < tableWidget->rowCount(); ++row) {
        int idx = findItemIndexByPath(rowPath(row));
        if (idx < 0) continue;
        GuardItem& item = items[idx];

        bool hasGuard = item.guarding;
        bool hasScheduledRestart = item.restartRulesActive && !item.restartRules.isEmpty();
        bool hasScheduledRun = item.scheduledRunEnabled && !item.runRules.isEmpty();

        if (!hasGuard && !hasScheduledRestart && !hasScheduledRun) continue;

        int count = 0;
        bool running = isProcessRunning(item.processName, count);
        bool scheduledRestarted = false;

        if (running && item.startDelayExitTime.isValid())
            item.startDelayExitTime = QDateTime();

        // 定时运行逻辑
        if (hasScheduledRun) {
            bool anyDue = false;
            for (ScheduleRule& rule : item.runRules) {
                if (rule.nextTrigger.isValid() && now >= rule.nextTrigger) {
                    anyDue = true;
                    rule.nextTrigger = calculateNextTrigger(rule, now);
                }
            }
            if (anyDue) {
                bool ok = launchProgram(item.targetPath, item.launchArgs);
                item.lastLaunchTime = now;
                item.lastRestart = now;
                item.restartCount++;
                appendScheduledRunLog(QString::fromUtf8("%1 \u5b9a\u65f6\u8fd0\u884c\u89e6\u53d1").arg(item.processName));
                if (!ok) {
                    trySendNotification(item, "run_failed",
                        QString::fromUtf8("%1 \u5b9a\u65f6\u8fd0\u884c\u542f\u52a8\u5931\u8d25").arg(item.processName));
                    if (!item.retryActive) {
                        item.retryActive = true;
                        item.retryStartTime = now;
                        item.currentRetryCount = 0;
                        item.lastRetryTime = now;
                    }
                }
            }
            auto setCell = [&](int col, const QString& text) {
                if (tableWidget->item(row, col)) {
                    tableWidget->item(row, col)->setText(text);
                    tableWidget->item(row, col)->setToolTip(text);
                }
            };
            setCell(1, QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c"));
            setCell(2, "-");
            setCell(3, item.lastRestart.isValid() ? item.lastRestart.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
            setCell(4, "-");
            QString rulesText = formatScheduleRules(item.runRules);
            setCell(5, rulesText);
            QDateTime nt = nextTriggerTime(item.runRules);
            setCell(6, nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
            setCell(7, "-");
            continue;
        }

        // 定时重启逻辑
        if (hasScheduledRestart) {
            bool anyDue = false;
            for (ScheduleRule& rule : item.restartRules) {
                if (rule.nextTrigger.isValid() && now >= rule.nextTrigger) {
                    anyDue = true;
                    rule.nextTrigger = calculateNextTrigger(rule, now);
                }
            }
            if (anyDue) {
                bool cooldown = item.lastGuardRestartTime.isValid()
                    && item.lastGuardRestartTime.secsTo(now) < 30;
                if (!cooldown) {
                    if (running) {
                        killProcessesByName(item.processName);
                        if (item.startDelaySecs > 0)
                            QThread::msleep(static_cast<unsigned long>(item.startDelaySecs * 1000));
                        bool ok = launchProgram(item.targetPath, item.launchArgs);
                        item.lastLaunchTime = now;
                        item.lastRestart = now;
                        item.startTime = now;
                        scheduledRestarted = true;
                        running = true;
                        appendScheduledRestartLog(QString::fromUtf8("%1 \u5b9a\u65f6\u91cd\u542f\u89e6\u53d1").arg(item.processName));
                        if (!ok) {
                            trySendNotification(item, "restart_failed",
                                QString::fromUtf8("%1 \u5b9a\u65f6\u91cd\u542f\u540e\u542f\u52a8\u5931\u8d25").arg(item.processName));
                            if (!item.retryActive) {
                                item.retryActive = true;
                                item.retryStartTime = now;
                                item.currentRetryCount = 0;
                                item.lastRetryTime = now;
                            }
                        }
                    }
                }
            }
        }

        // 守护逻辑
        if (hasGuard && !running && !scheduledRestarted) {
            if (!item.lastLaunchTime.isValid() || item.lastLaunchTime.secsTo(now) >= 10) {
                int recount = 0;
                isProcessRunning(item.processName, recount);
                if (recount == 0) {
                    if (item.startDelaySecs > 0) {
                        if (!item.startDelayExitTime.isValid()) {
                            item.startDelayExitTime = now;
                            trySendNotification(item, "process_exited",
                                QString::fromUtf8("%1 \u8fdb\u7a0b\u5df2\u9000\u51fa\uff0c\u5c06\u5728 %2 \u79d2\u540e\u91cd\u542f").arg(item.processName).arg(item.startDelaySecs));
                        }
                        if (item.startDelayExitTime.secsTo(now) >= item.startDelaySecs) {
                            item.startDelayExitTime = QDateTime();
                            bool ok = launchProgram(item.targetPath, item.launchArgs);
                            item.lastLaunchTime = now;
                            item.lastGuardRestartTime = now;
                            item.restartCount++;
                            item.lastRestart = now;
                            item.startTime = now;
                            running = true;
                            trySendNotification(item, "guard_triggered",
                                QString::fromUtf8("%1 \u5b88\u62a4\u89e6\u53d1\u91cd\u542f").arg(item.processName));
                            if (!ok) {
                                trySendNotification(item, "start_failed",
                                    QString::fromUtf8("%1 \u5b88\u62a4\u542f\u52a8\u5931\u8d25").arg(item.processName));
                                if (!item.retryActive) {
                                    item.retryActive = true;
                                    item.retryStartTime = now;
                                    item.currentRetryCount = 0;
                                    item.lastRetryTime = now;
                                }
                            }
                        }
                    } else {
                        bool ok = launchProgram(item.targetPath, item.launchArgs);
                        item.lastLaunchTime = now;
                        item.lastGuardRestartTime = now;
                        item.restartCount++;
                        item.lastRestart = now;
                        item.startTime = now;
                        running = true;
                        trySendNotification(item, "guard_triggered",
                            QString::fromUtf8("%1 \u5b88\u62a4\u89e6\u53d1\u91cd\u542f").arg(item.processName));
                        if (!ok) {
                            trySendNotification(item, "start_failed",
                                QString::fromUtf8("%1 \u5b88\u62a4\u542f\u52a8\u5931\u8d25").arg(item.processName));
                            if (!item.retryActive) {
                                item.retryActive = true;
                                item.retryStartTime = now;
                                item.currentRetryCount = 0;
                                item.lastRetryTime = now;
                            }
                        }
                    }
                }
            }
        }

        // 重试逻辑
        if (item.retryActive && !running) {
            bool expired = false;
            if (item.retryConfig.maxRetries > 0 && item.currentRetryCount >= item.retryConfig.maxRetries) expired = true;
            if (item.retryConfig.maxDurationSecs > 0 && item.retryStartTime.secsTo(now) >= item.retryConfig.maxDurationSecs) expired = true;
            if (expired) {
                item.retryActive = false;
                trySendNotification(item, "retry_exhausted",
                    QString::fromUtf8("%1 \u91cd\u8bd5\u5df2\u8017\u5c3d\uff0c\u5171\u91cd\u8bd5 %2 \u6b21").arg(item.processName).arg(item.currentRetryCount));
            } else if (item.lastRetryTime.secsTo(now) >= item.retryConfig.retryIntervalSecs) {
                bool ok = launchProgram(item.targetPath, item.launchArgs);
                item.lastRetryTime = now;
                item.currentRetryCount++;
                item.lastLaunchTime = now;
                if (ok) {
                    item.retryActive = false;
                    item.startTime = now;
                    running = true;
                }
            }
        }
        if (running && item.retryActive) item.retryActive = false;

        if (running) {
            item.notifiedStartFailed = false;
            item.notifiedRestartFailed = false;
            item.notifiedRunFailed = false;
            item.notifiedRetryExhausted = false;
        }

        // 更新UI
        auto setCell = [&](int col, const QString& text) {
            if (tableWidget->item(row, col)) {
                tableWidget->item(row, col)->setText(text);
                tableWidget->item(row, col)->setToolTip(text);
            }
        };
        if (hasGuard) {
            setCell(1, running ? QString::fromUtf8("\u8fd0\u884c\u4e2d") : QString::fromUtf8("\u5df2\u91cd\u542f"));
            qint64 secs = item.startTime.isValid() ? item.startTime.secsTo(now) : 0;
            setCell(2, [&](qint64 s) -> QString {
                qint64 days = s / 86400;
                qint64 hours = (s % 86400) / 3600;
                qint64 mins = (s % 3600) / 60;
                if (days > 0) return QString::number(days) + QString::fromUtf8("\u5929") + QString::number(hours) + QString::fromUtf8("\u65f6") + QString::number(mins) + QString::fromUtf8("\u5206");
                if (hours > 0) return QString::number(hours) + QString::fromUtf8("\u65f6") + QString::number(mins) + QString::fromUtf8("\u5206");
                return QString::number(mins) + QString::fromUtf8("\u5206");
            }(secs));
        } else if (hasScheduledRestart) {
            setCell(1, running ? QString::fromUtf8("\u8fd0\u884c\u4e2d") : QString::fromUtf8("\u672a\u8fd0\u884c"));
            setCell(2, "-");
        }
        setCell(3, item.lastRestart.isValid() ? item.lastRestart.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
        setCell(4, QString::number(item.restartCount));
        setCell(5, item.restartRulesActive ? formatScheduleRules(item.restartRules) : QStringLiteral("-"));
        QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
        setCell(6, nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
        setCell(7, formatStartDelay(item.startDelaySecs));
    }

    // 动态列排序：当排序列内容变化时更新排序
    if (sortState != 0 && activeSortSection >= 1 && activeSortSection <= 7) {
        Qt::SortOrder order = (sortState == 1) ? Qt::AscendingOrder : Qt::DescendingOrder;
        bool orderChanged = false;
        for (int r = 0; r < tableWidget->rowCount() - 1; r++) {
            int idx1 = findItemIndexByPath(rowPath(r));
            int idx2 = findItemIndexByPath(rowPath(r + 1));
            if (idx1 < 0 || idx2 < 0) continue;
            if (items[idx1].pinned != items[idx2].pinned) continue;
            QTableWidgetItem* it1 = tableWidget->item(r, activeSortSection);
            QTableWidgetItem* it2 = tableWidget->item(r + 1, activeSortSection);
            QString t1 = it1 ? it1->text() : QString();
            QString t2 = it2 ? it2->text() : QString();
            int cmp = t1.localeAwareCompare(t2);
            if ((order == Qt::AscendingOrder && cmp > 0) || (order == Qt::DescendingOrder && cmp < 0)) {
                orderChanged = true;
                break;
            }
        }
        if (orderChanged) performSort();
    }

    saveSettings();
}
