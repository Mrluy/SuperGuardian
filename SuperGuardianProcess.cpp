#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "AppStorage.h"
#include <QtWidgets>

// ---- 表格行创建与按钮状态管理 ----

void SuperGuardian::setupTableRow(int row, const GuardItem& item) {
    auto makeItem = [](const QString& t) {
        QTableWidgetItem* it = new QTableWidgetItem(t);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setTextAlignment(Qt::AlignCenter);
        it->setToolTip(t);
        return it;
    };
    QString displayName = item.note.isEmpty()
        ? (item.launchArgs.isEmpty() ? item.processName : (item.processName + " " + item.launchArgs))
        : item.note;
    QString tooltipName = item.launchArgs.isEmpty() ? item.processName : (item.processName + " " + item.launchArgs);
    QTableWidgetItem* nameItem = new QTableWidgetItem(displayName);
    nameItem->setIcon(getFileIcon(item.targetPath));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole, item.path);
    nameItem->setData(Qt::UserRole + 2, item.pinned);
    nameItem->setToolTip(tooltipName);
    tableWidget->setItem(row, 0, nameItem);

    QString initStatus;
    if (item.scheduledRunEnabled) initStatus = QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c");
    else if (item.guarding) initStatus = QString::fromUtf8("\u8fd0\u884c\u4e2d");
    else if (item.restartRulesActive) initStatus = QStringLiteral("-");
    else initStatus = QString::fromUtf8("\u672a\u5b88\u62a4");

    tableWidget->setItem(row, 1, makeItem(initStatus));

    // col 2: 持续运行时长 — 守护中时从 startTime 计算实际时长
    QString durText = QStringLiteral("-");
    if (item.guarding && item.startTime.isValid()) {
        qint64 secs = item.startTime.secsTo(QDateTime::currentDateTime());
        if (secs < 0) secs = 0;
        qint64 days = secs / 86400;
        qint64 hours = (secs % 86400) / 3600;
        qint64 mins = (secs % 3600) / 60;
        if (days > 0) durText = QString::number(days) + QString::fromUtf8("\u5929") + QString::number(hours) + QString::fromUtf8("\u65f6") + QString::number(mins) + QString::fromUtf8("\u5206");
        else if (hours > 0) durText = QString::number(hours) + QString::fromUtf8("\u65f6") + QString::number(mins) + QString::fromUtf8("\u5206");
        else durText = QString::number(mins) + QString::fromUtf8("\u5206");
    }
    tableWidget->setItem(row, 2, makeItem(durText));

    tableWidget->setItem(row, 3, makeItem(item.lastRestart.isValid() ? item.lastRestart.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-"));
    tableWidget->setItem(row, 4, makeItem(item.scheduledRunEnabled ? QStringLiteral("-") : QString::number(item.restartCount)));

    // col 5/6: 定时运行时显示 runRules，否则显示 restartRules
    if (item.scheduledRunEnabled && !item.runRules.isEmpty()) {
        tableWidget->setItem(row, 5, makeItem(formatScheduleRules(item.runRules)));
        QDateTime nt = nextTriggerTime(item.runRules);
        tableWidget->setItem(row, 6, makeItem(nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-"));
    } else {
        tableWidget->setItem(row, 5, makeItem(item.restartRulesActive ? formatScheduleRules(item.restartRules) : QStringLiteral("-")));
        QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
        tableWidget->setItem(row, 6, makeItem(nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-"));
    }

    tableWidget->setItem(row, 7, makeItem(item.scheduledRunEnabled ? QStringLiteral("-") : formatStartDelay(item.startDelaySecs)));

    // 3 buttons: 开始守护/关闭守护, 开启定时重启/关闭定时重启, 开启定时运行/关闭定时运行
    QWidget* opWidget = new QWidget();
    QHBoxLayout* opLay = new QHBoxLayout(opWidget);
    opLay->setContentsMargins(2, 0, 2, 0);
    opLay->setSpacing(2);

    QPushButton* guardBtn = new QPushButton(item.guarding ? QString::fromUtf8("\u5173\u95ed\u5b88\u62a4") : QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
    guardBtn->setObjectName(QString("guardBtn_%1").arg(item.path));
    guardBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    guardBtn->setToolTip(QString::fromUtf8("\u4fdd\u62a4\u76ee\u6807\u7a0b\u5e8f\uff0c\u907f\u514d\u5176\u88ab\u610f\u5916\u6216\u5f02\u5e38\u5173\u95ed"));
    QPushButton* srBtn = new QPushButton(item.restartRulesActive ? QString::fromUtf8("\u5173\u95ed\u5b9a\u65f6\u91cd\u542f") : QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u91cd\u542f"));
    srBtn->setObjectName(QString("srBtn_%1").arg(item.path));
    srBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    srBtn->setToolTip(QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u76ee\u6807\u7a0b\u5e8f\uff0c\u53ef\u6dfb\u52a0\u4efb\u610f\u7ec4\u5b9a\u65f6\u8ba1\u5212"));
    QPushButton* runBtn = new QPushButton(item.scheduledRunEnabled ? QString::fromUtf8("\u5173\u95ed\u5b9a\u65f6\u8fd0\u884c") : QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u8fd0\u884c"));
    runBtn->setObjectName(QString("runBtn_%1").arg(item.path));
    runBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    runBtn->setToolTip(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c\u76ee\u6807\u7a0b\u5e8f\uff0c\u53ef\u6dfb\u52a0\u4efb\u610f\u7ec4\u5b9a\u65f6\u8ba1\u5212"));

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
                launchProgram(it.targetPath, it.launchArgs);
                it.lastLaunchTime = QDateTime::currentDateTime();
            }
        } else {
            it.restartCount = 0;
            if (!it.restartRulesActive) {
                if (tableWidget->item(displayRow, 1)) tableWidget->item(displayRow, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
            }
            if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText("-");
            if (tableWidget->item(displayRow, 4)) tableWidget->item(displayRow, 4)->setText("0");
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
            if (tableWidget->item(displayRow, 7)) tableWidget->item(displayRow, 7)->setText(formatStartDelay(it.startDelaySecs));
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

void SuperGuardian::addProgram(const QString& path, const QString& extraArgs) {
    QString resolvedPath = path;
    QFileInfo fi(path);
    if (!fi.exists()) {
        QString found = QStandardPaths::findExecutable(path);
        if (found.isEmpty()) return;
        resolvedPath = found;
    }
    for (const GuardItem& it : items) if (it.path == resolvedPath) return;

    GuardItem item;
    item.path = resolvedPath;
    QString shortcutArgs;
    item.targetPath = resolveShortcut(resolvedPath, &shortcutArgs);
    if (!extraArgs.isEmpty())
        item.launchArgs = shortcutArgs.isEmpty() ? extraArgs : (shortcutArgs + " " + extraArgs);
    else
        item.launchArgs = shortcutArgs;
    item.processName = QFileInfo(item.targetPath).fileName();
    item.guarding = false;
    items.append(item);

    rebuildTableFromItems();
    saveSettings();
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
