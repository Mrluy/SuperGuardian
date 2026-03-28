#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include <QtWidgets>
#include <QThread>

// ---- 表格行创建与按钮状态管理 ----

void SuperGuardian::setupTableRow(int row, const GuardItem& item) {
    auto makeItem = [](const QString& t) {
        QTableWidgetItem* it = new QTableWidgetItem(t);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setTextAlignment(Qt::AlignCenter);
        it->setToolTip(t);
        return it;
    };
    QTableWidgetItem* nameItem = new QTableWidgetItem(item.processName);
    nameItem->setIcon(getFileIcon(item.targetPath));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole, item.path);
    nameItem->setToolTip(item.processName);
    tableWidget->setItem(row, 0, nameItem);

    QString initStatus;
    if (item.scheduledRunEnabled) initStatus = QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c");
    else if (item.guarding) initStatus = QString::fromUtf8("\u8fd0\u884c\u4e2d");
    else if (item.restartRulesActive) initStatus = QStringLiteral("-");
    else initStatus = QString::fromUtf8("\u672a\u5b88\u62a4");

    tableWidget->setItem(row, 1, makeItem(initStatus));
    tableWidget->setItem(row, 2, makeItem(item.guarding ? QStringLiteral("0") : QStringLiteral("-")));
    tableWidget->setItem(row, 3, makeItem(item.lastRestart.isValid() ? item.lastRestart.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-"));
    tableWidget->setItem(row, 4, makeItem(QString::number(item.restartCount)));

    // col 5/6 始终显示定时重启规则
    tableWidget->setItem(row, 5, makeItem(item.restartRulesActive ? formatScheduleRules(item.restartRules) : QStringLiteral("-")));
    QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
    tableWidget->setItem(row, 6, makeItem(nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-"));
    tableWidget->setItem(row, 7, makeItem(item.scheduledRunEnabled ? QStringLiteral("-") : (QString::number(item.startDelaySecs) + QString::fromUtf8(" \u79d2"))));

    // 3 buttons: 开始守护/关闭守护, 开启定时重启/关闭定时重启, 开启定时运行/关闭定时运行
    QWidget* opWidget = new QWidget();
    QHBoxLayout* opLay = new QHBoxLayout(opWidget);
    opLay->setContentsMargins(2, 0, 2, 0);
    opLay->setSpacing(2);

    QPushButton* guardBtn = new QPushButton(item.guarding ? QString::fromUtf8("\u5173\u95ed\u5b88\u62a4") : QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
    guardBtn->setObjectName(QString("guardBtn_%1").arg(item.path));
    guardBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    QPushButton* srBtn = new QPushButton(item.restartRulesActive ? QString::fromUtf8("\u5173\u95ed\u5b9a\u65f6\u91cd\u542f") : QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u91cd\u542f"));
    srBtn->setObjectName(QString("srBtn_%1").arg(item.path));
    srBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    QPushButton* runBtn = new QPushButton(item.scheduledRunEnabled ? QString::fromUtf8("\u5173\u95ed\u5b9a\u65f6\u8fd0\u884c") : QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u8fd0\u884c"));
    runBtn->setObjectName(QString("runBtn_%1").arg(item.path));
    runBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    opLay->addWidget(guardBtn);
    opLay->addWidget(srBtn);
    opLay->addWidget(runBtn);
    tableWidget->setCellWidget(row, 8, opWidget);

    // Guard button
    connect(guardBtn, &QPushButton::clicked, this, [this, itemPath = item.path]() {
        int idx = findItemIndexByPath(itemPath);
        if (idx < 0) return;
        int displayRow = findRowByPath(itemPath);
        if (displayRow < 0) return;
        GuardItem& it = items[idx];
        it.guarding = !it.guarding;
        QPushButton* b = qobject_cast<QPushButton*>(sender());
        if (b) b->setText(it.guarding ? QString::fromUtf8("\u5173\u95ed\u5b88\u62a4") : QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
        if (it.guarding) {
            it.startTime = QDateTime::currentDateTime();
            int count = 0;
            bool running = isProcessRunning(it.processName, count);
            if (!running && count == 0) {
                launchProgram(it.path);
                it.lastLaunchTime = QDateTime::currentDateTime();
            }
        } else {
            if (!it.restartRulesActive) {
                if (tableWidget->item(displayRow, 1)) tableWidget->item(displayRow, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
            }
            if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText("-");
        }
        updateButtonStates(displayRow);
        saveSettings();
    });

    // Scheduled restart button
    connect(srBtn, &QPushButton::clicked, this, [this, itemPath = item.path]() {
        int idx = findItemIndexByPath(itemPath);
        if (idx < 0) return;
        int displayRow = findRowByPath(itemPath);
        if (displayRow < 0) return;
        GuardItem& it = items[idx];
        if (it.restartRulesActive) {
            it.restartRulesActive = false;
            QPushButton* b = qobject_cast<QPushButton*>(sender());
            if (b) b->setText(QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u91cd\u542f"));
            if (tableWidget->item(displayRow, 5)) tableWidget->item(displayRow, 5)->setText("-");
            if (tableWidget->item(displayRow, 6)) tableWidget->item(displayRow, 6)->setText("-");
            updateButtonStates(displayRow);
            saveSettings();
        } else {
            contextSetScheduleRules(QList<int>{displayRow}, false);
        }
    });

    // Scheduled run button
    connect(runBtn, &QPushButton::clicked, this, [this, itemPath = item.path]() {
        int idx = findItemIndexByPath(itemPath);
        if (idx < 0) return;
        int displayRow = findRowByPath(itemPath);
        if (displayRow < 0) return;
        GuardItem& it = items[idx];
        if (it.scheduledRunEnabled) {
            it.scheduledRunEnabled = false;
            QPushButton* b = qobject_cast<QPushButton*>(sender());
            if (b) b->setText(QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u8fd0\u884c"));
            if (tableWidget->item(displayRow, 1)) tableWidget->item(displayRow, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
            if (tableWidget->item(displayRow, 7)) tableWidget->item(displayRow, 7)->setText(QString::number(it.startDelaySecs) + QString::fromUtf8(" \u79d2"));
            updateButtonStates(displayRow);
            saveSettings();
        } else {
            contextSetScheduleRules(QList<int>{displayRow}, true);
        }
    });

    if (tableWidget->selectionModel())
        tableWidget->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);

    updateButtonStates(row);
}

void SuperGuardian::updateButtonStates(int row) {
    QWidget* opw = tableWidget->cellWidget(row, 8);
    if (!opw) return;
    QString path = rowPath(row);
    int idx = findItemIndexByPath(path);
    if (idx < 0) return;
    const GuardItem& it = items[idx];

    QPushButton* guardBtn = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(path));
    QPushButton* srBtn = opw->findChild<QPushButton*>(QString("srBtn_%1").arg(path));
    QPushButton* runBtn = opw->findChild<QPushButton*>(QString("runBtn_%1").arg(path));

    bool guardOrRestartActive = it.guarding || it.restartRulesActive;
    bool runActive = it.scheduledRunEnabled;

    if (guardBtn) guardBtn->setEnabled(!runActive);
    if (srBtn) srBtn->setEnabled(!runActive);
    if (runBtn) runBtn->setEnabled(!guardOrRestartActive);
}

// ---- 程序添加 ----

void SuperGuardian::addProgram(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return;
    for (const GuardItem& it : items) if (it.path == path) return;

    GuardItem item;
    item.path = path;
    item.targetPath = resolveShortcut(path);
    item.processName = QFileInfo(item.targetPath).fileName();
    item.guarding = false;
    items.append(item);

    int row = tableWidget->rowCount();
    tableWidget->insertRow(row);
    setupTableRow(row, item);
}

// ---- 邮件通知 ----

void SuperGuardian::trySendNotification(GuardItem& item, const QString& event, const QString& detail) {
    if (!emailEnabledAct || !emailEnabledAct->isChecked()) return;
    if (!item.emailNotify.enabled) return;
    if (!isSmtpConfigValid(smtpConfig)) return;

    bool shouldSend = false;
    if (event == "guard_triggered") shouldSend = item.emailNotify.onGuardTriggered;
    else if (event == "start_failed") {
        if (item.notifiedStartFailed) return;
        shouldSend = item.emailNotify.onStartFailed;
        if (shouldSend) item.notifiedStartFailed = true;
    }
    else if (event == "restart_failed") {
        if (item.notifiedRestartFailed) return;
        shouldSend = item.emailNotify.onScheduledRestartFailed;
        if (shouldSend) item.notifiedRestartFailed = true;
    }
    else if (event == "run_failed") {
        if (item.notifiedRunFailed) return;
        shouldSend = item.emailNotify.onScheduledRunFailed;
        if (shouldSend) item.notifiedRunFailed = true;
    }
    else if (event == "process_exited") shouldSend = item.emailNotify.onProcessExited;
    else if (event == "retry_exhausted") {
        if (item.notifiedRetryExhausted) return;
        shouldSend = item.emailNotify.onRetryExhausted;
        if (shouldSend) item.notifiedRetryExhausted = true;
    }
    if (!shouldSend) return;

    QString subject = QString::fromUtf8("[SuperGuardian] %1 - %2").arg(item.processName, event);
    sendNotificationAsync(smtpConfig, subject, detail);
}

// ---- 进程监控 ----

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

        // 进程运行中，重置启动延时计时
        if (running && item.startDelayExitTime.isValid()) {
            item.startDelayExitTime = QDateTime();
        }

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
                bool ok = launchProgram(item.path);
                item.lastLaunchTime = now;
                item.lastRestart = now;
                item.restartCount++;
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
            // Update UI for scheduled run
            if (tableWidget->item(row, 1)) { tableWidget->item(row, 1)->setText(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c")); tableWidget->item(row, 1)->setToolTip(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c")); }
            if (tableWidget->item(row, 2)) { tableWidget->item(row, 2)->setText("-"); tableWidget->item(row, 2)->setToolTip("-"); }
            QDateTime nt = nextTriggerTime(item.runRules);
            if (tableWidget->item(row, 5)) { QString t = formatScheduleRules(item.runRules); tableWidget->item(row, 5)->setText(t); tableWidget->item(row, 5)->setToolTip(t); }
            if (tableWidget->item(row, 6)) { QString t = nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-"; tableWidget->item(row, 6)->setText(t); tableWidget->item(row, 6)->setToolTip(t); }
            if (tableWidget->item(row, 7)) { tableWidget->item(row, 7)->setText("-"); tableWidget->item(row, 7)->setToolTip("-"); }
            continue;
        }

        // 定时重启逻辑（独立于守护）
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
                        QThread::msleep(static_cast<unsigned long>(item.startDelaySecs * 1000));
                        bool ok = launchProgram(item.path);
                        item.lastLaunchTime = now;
                        item.lastRestart = now;
                        scheduledRestarted = true;
                        running = true;
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

        // 守护逻辑（仅当守护开启时）
        if (hasGuard && !running && !scheduledRestarted) {
            if (item.lastLaunchTime.isValid() && item.lastLaunchTime.secsTo(now) < 10) {
                // 刚启动过，跳过
            } else {
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
                            bool ok = launchProgram(item.path);
                            item.lastLaunchTime = now;
                            item.lastGuardRestartTime = now;
                            item.restartCount++;
                            item.lastRestart = now;
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
                        bool ok = launchProgram(item.path);
                        item.lastLaunchTime = now;
                        item.lastGuardRestartTime = now;
                        item.restartCount++;
                        item.lastRestart = now;
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
                bool ok = launchProgram(item.path);
                item.lastRetryTime = now;
                item.currentRetryCount++;
                item.lastLaunchTime = now;
                if (ok) {
                    item.retryActive = false;
                    running = true;
                }
            }
        }
        if (running && item.retryActive) item.retryActive = false;

        // 进程成功运行，重置失败通知去重标记
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
        setCell(7, QString::number(item.startDelaySecs) + QString::fromUtf8(" \u79d2"));
    }
    saveSettings();
}
