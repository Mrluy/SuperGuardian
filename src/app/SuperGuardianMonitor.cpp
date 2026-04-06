#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QThread>

// ---- 进程监控主循环 ----

void SuperGuardian::checkProcesses() {
    QDateTime now = QDateTime::currentDateTime();

    // 全局功能开关
    bool globalGuardOn = globalGuardAct && globalGuardAct->isChecked();
    bool globalRestartOn = globalRestartAct && globalRestartAct->isChecked();
    bool globalRunOn = globalRunAct && globalRunAct->isChecked();

    for (int row = 0; row < tableWidget->rowCount(); ++row) {
        int idx = findItemIndexById(rowId(row));
        if (idx < 0) continue;
        GuardItem& item = items[idx];

        bool hasGuard = item.guarding && globalGuardOn;
        bool hasScheduledRestart = item.restartRulesActive && !item.restartRules.isEmpty() && globalRestartOn;
        bool hasScheduledRun = item.scheduledRunEnabled && !item.runRules.isEmpty() && globalRunOn;

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
                logScheduledRun(u"定时运行触发"_s, programId(item.processName, item.launchArgs));
                if (!ok) {
                    trySendNotification(item, "run_failed",
                        u"%1 定时运行启动失败"_s.arg(item.processName));
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
            setCell(2, u"定时运行"_s);
            {
                if (item.trackRunDuration) {
                    QDateTime procStart = getProcessStartTime(item.processName);
                    setCell(3, procStart.isValid() ? formatDuration(procStart.secsTo(now)) : "-");
                } else {
                    setCell(3, "-");
                }
            }
            setCell(4, item.lastRestart.isValid() ? item.lastRestart.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
            setCell(5, "-");
            setCell(6, "-");
            QString rulesText = formatScheduleRules(item.runRules);
            setCell(7, rulesText);
            QDateTime nt = nextTriggerTime(item.runRules);
            setCell(8, nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
            setCell(9, "-");
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
                        item.startDelayExitTime = QDateTime();
                        item.lastGuardRestartTime = now;
                        running = true;
                        logScheduledRestart(u"定时重启触发"_s, programId(item.processName, item.launchArgs));
                        if (!ok) {
                            trySendNotification(item, "restart_failed",
                                u"%1 定时重启后启动失败"_s.arg(item.processName));
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
                    auto tryGuardLaunch = [&]() {
                        int finalCount = 0;
                        if (isProcessRunning(item.processName, finalCount) || finalCount > 0) {
                            item.startDelayExitTime = QDateTime();
                            running = true;
                            return;
                        }

                        bool ok = launchProgram(item.targetPath, item.launchArgs);
                        item.lastLaunchTime = now;
                        item.lastGuardRestartTime = now;
                        item.restartCount++;
                        item.lastRestart = now;
                        item.startTime = now;
                        running = true;
                        trySendNotification(item, "guard_triggered",
                            u"%1 守护触发重启"_s.arg(item.processName));
                        logGuard(u"守护触发重启"_s, programId(item.processName, item.launchArgs));
                        if (!ok) {
                            trySendNotification(item, "start_failed",
                                u"%1 守护启动失败"_s.arg(item.processName));
                            logGuard(u"守护启动失败"_s, programId(item.processName, item.launchArgs));
                            if (!item.retryActive) {
                                item.retryActive = true;
                                item.retryStartTime = now;
                                item.currentRetryCount = 0;
                                item.lastRetryTime = now;
                            }
                        }
                    };

                    if (item.startDelaySecs > 0) {
                        if (!item.startDelayExitTime.isValid()) {
                            item.startDelayExitTime = now;
                            trySendNotification(item, "process_exited",
                                u"%1 进程已退出，将在 %2 秒后重启"_s.arg(item.processName).arg(item.startDelaySecs));
                        }
                        if (item.startDelayExitTime.msecsTo(now) >= item.startDelaySecs * 1000) {
                            tryGuardLaunch();
                        }
                    } else {
                        tryGuardLaunch();
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
                    u"%1 重试已耗尽，共重试 %2 次"_s.arg(item.processName).arg(item.currentRetryCount));
                logRuntime(u"重试已耗尽，共重试 %1 次"_s.arg(item.currentRetryCount), programId(item.processName, item.launchArgs));
            } else if (item.lastRetryTime.secsTo(now) >= item.retryConfig.retryIntervalSecs) {
                bool ok = launchProgram(item.targetPath, item.launchArgs);
                item.lastRetryTime = now;
                item.currentRetryCount++;
                item.lastLaunchTime = now;
                logRuntime(u"重试启动（第%1次）"_s.arg(item.currentRetryCount), programId(item.processName, item.launchArgs));
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
            setCell(2, running ? u"运行中"_s : u"已重启"_s);
        } else if (hasScheduledRestart) {
            setCell(2, running ? u"运行中"_s : u"未运行"_s);
        }
        if (hasGuard) {
            QDateTime procStart = getProcessStartTime(item.processName);
            setCell(3, procStart.isValid() ? formatDuration(procStart.secsTo(now)) : "-");
        } else {
            setCell(3, "-");
        }
        if (item.guarding && item.guardStartTime.isValid()) {
            setCell(6, formatDuration(item.guardStartTime.secsTo(now)));
        } else {
            setCell(6, "-");
        }
        setCell(4, item.lastRestart.isValid() ? item.lastRestart.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
        setCell(5, QString::number(item.restartCount));
        setCell(7, item.restartRulesActive ? formatScheduleRules(item.restartRules) : u"-"_s);
        QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
        setCell(8, nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
        setCell(9, formatStartDelay(item.startDelaySecs));
    }

    // 动态列排序：当排序列内容变化时更新排序
    if (sortState != 0 && activeSortSection >= 2 && activeSortSection <= 9) {
        Qt::SortOrder order = (sortState == 1) ? Qt::AscendingOrder : Qt::DescendingOrder;
        bool orderChanged = false;
        for (int r = 0; r < tableWidget->rowCount() - 1; r++) {
            int idx1 = findItemIndexById(rowId(r));
            int idx2 = findItemIndexById(rowId(r + 1));
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
