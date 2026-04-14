#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include "ThemeManager.h"
#include <QtWidgets>

using namespace Qt::Literals::StringLiterals;

void SuperGuardian::setupTableRow(int row, const GuardItem& item) {
    auto makeItem = [](const QString& t) {
        QTableWidgetItem* it = new QTableWidgetItem(t);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setTextAlignment(Qt::AlignCenter);
        it->setToolTip(t);
        return it;
    };

    // col 0: UUID
    QTableWidgetItem* uuidItem = makeItem(item.id.left(8));
    uuidItem->setData(Qt::UserRole, item.id);
    uuidItem->setToolTip(item.id);
    tableWidget->setItem(row, 0, uuidItem);

    // col 1: 程序
    QString displayName = item.note.isEmpty()
        ? (item.launchArgs.isEmpty() ? item.processName : (item.processName + " " + item.launchArgs))
        : item.note;
    QString tooltipName = item.launchArgs.isEmpty() ? item.processName : (item.processName + " " + item.launchArgs);
    QTableWidgetItem* nameItem = new QTableWidgetItem(displayName);
    nameItem->setIcon(getFileIcon(item.targetPath));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole + 2, item.pinned);
    nameItem->setToolTip(item.id.left(8) + u" "_s + tooltipName);
    tableWidget->setItem(row, 1, nameItem);

    QString initStatus;
    if (item.scheduledRunEnabled) initStatus = u"定时运行"_s;
    else if (item.guarding) initStatus = u"运行中"_s;
    else if (item.restartRulesActive) initStatus = u"-"_s;
    else initStatus = u"未守护"_s;

    tableWidget->setItem(row, 2, makeItem(initStatus));

    // col 3: 持续运行时长 — 操作系统中的进程持续运行时间
    QString durText = u"-"_s;
    if (!(item.scheduledRunEnabled && !item.trackRunDuration)) {
        QDateTime procStart = getProcessStartTime(item.processName);
        if (procStart.isValid()) {
            qint64 secs = procStart.secsTo(QDateTime::currentDateTime());
            if (secs < 0) secs = 0;
            durText = formatDuration(secs);
        }
    }
    tableWidget->setItem(row, 3, makeItem(durText));

    tableWidget->setItem(row, 4, makeItem(item.lastRestart.isValid() ? item.lastRestart.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-"));
    tableWidget->setItem(row, 5, makeItem(item.scheduledRunEnabled ? u"-"_s : QString::number(item.restartCount)));

    // col 6: 持续守护时长
    QString guardDurText = u"-"_s;
    if (item.guarding && item.guardStartTime.isValid()) {
        qint64 secs = item.guardStartTime.secsTo(QDateTime::currentDateTime());
        if (secs < 0) secs = 0;
        guardDurText = formatDuration(secs);
    }
    tableWidget->setItem(row, 6, makeItem(guardDurText));

    // col 7/8: 定时运行时显示 runRules，否则显示 restartRules
    if (item.scheduledRunEnabled && !item.runRules.isEmpty()) {
        QTableWidgetItem* ruleItem = makeItem(formatScheduleRules(item.runRules));
        ruleItem->setToolTip(formatScheduleRulesDetail(item.runRules));
        tableWidget->setItem(row, 7, ruleItem);
        QDateTime nt = nextTriggerTime(item.runRules);
        tableWidget->setItem(row, 8, makeItem(nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-"));

        // 隐藏窗口图标
        QString iconDir = (currentThemeName() == u"dark"_s) ? u"light"_s : u"dark"_s;
        QIcon hideIcon(u":/SuperGuardian/%1/hide.png"_s.arg(iconDir));
        if (QTableWidgetItem* lastCell = tableWidget->item(row, 4))
            lastCell->setIcon(item.lastRunHidden ? hideIcon : QIcon());
        if (QTableWidgetItem* nextCell = tableWidget->item(row, 8))
            nextCell->setIcon(item.runHideWindow ? hideIcon : QIcon());
    } else {
        QTableWidgetItem* ruleItem = makeItem(item.restartRulesActive ? formatScheduleRules(item.restartRules) : u"-"_s);
        if (item.restartRulesActive)
            ruleItem->setToolTip(formatScheduleRulesDetail(item.restartRules));
        tableWidget->setItem(row, 7, ruleItem);
        QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
        tableWidget->setItem(row, 8, makeItem(nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-"));
    }

    tableWidget->setItem(row, 9, makeItem(item.scheduledRunEnabled ? u"-"_s : formatStartDelay(item.startDelaySecs)));

    // 3 buttons: 开始守护/关闭守护, 开启定时重启/关闭定时重启, 开启定时运行/关闭定时运行
    QWidget* opWidget = new QWidget();
    opWidget->setObjectName(u"opContainer"_s);
    opWidget->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* opLay = new QHBoxLayout(opWidget);
    opLay->setContentsMargins(2, 0, 2, 0);
    opLay->setSpacing(2);

    QPushButton* guardBtn = new QPushButton(item.guarding ? u"关闭守护"_s : u"开始守护"_s);
    guardBtn->setObjectName(QString("guardBtn_%1").arg(item.id));
    guardBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    guardBtn->setToolTip(u"保护目标程序，避免其被意外或异常关闭"_s);
    QPushButton* srBtn = new QPushButton(item.restartRulesActive ? u"关闭定时重启"_s : u"开启定时重启"_s);
    srBtn->setObjectName(QString("srBtn_%1").arg(item.id));
    srBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    srBtn->setToolTip(u"定时重启目标程序，可添加任意组定时计划"_s);
    QPushButton* runBtn = new QPushButton(item.scheduledRunEnabled ? u"关闭定时运行"_s : u"开启定时运行"_s);
    runBtn->setObjectName(QString("runBtn_%1").arg(item.id));
    runBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    runBtn->setToolTip(u"定时运行目标程序，可添加任意组定时计划"_s);

    opLay->addWidget(guardBtn);
    opLay->addWidget(srBtn);
    opLay->addWidget(runBtn);
    tableWidget->setCellWidget(row, 10, opWidget);

    auto setupContextMenu = [this, itemId = item.id](QWidget* w) {
        w->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(w, &QWidget::customContextMenuRequested, this,
            [this, w, itemId](const QPoint& pos) {
                int displayRow = findRowById(itemId);
                if (displayRow < 0)
                    return;
                tableWidget->selectRow(displayRow);
                onTableContextMenuRequested(tableWidget->viewport()->mapFromGlobal(w->mapToGlobal(pos)));
            });
    };
    setupContextMenu(opWidget);
    setupContextMenu(guardBtn);
    setupContextMenu(srBtn);
    setupContextMenu(runBtn);

    // Guard button
    connect(guardBtn, &QPushButton::clicked, this, [this, itemId = item.id]() {
        int idx = findItemIndexById(itemId);
        if (idx < 0) return;
        int displayRow = findRowById(itemId);
        if (displayRow < 0) return;
        GuardItem& it = items[idx];
        it.guarding = !it.guarding;
        QPushButton* b = qobject_cast<QPushButton*>(sender());
        if (b) b->setText(it.guarding ? u"关闭守护"_s : u"开始守护"_s);
        if (it.guarding) {
            logOperation(u"开始守护"_s, programId(it.processName, it.launchArgs));
            const bool globalGuardOn = globalGuardAct && globalGuardAct->isChecked();
            if (globalGuardOn) {
                it.startTime = QDateTime::currentDateTime();
                it.guardStartTime = QDateTime::currentDateTime();
                int count = 0;
                bool running = isProcessRunning(it.processName, count);
                if (!running && count == 0) {
                    launchProgram(it.targetPath, it.launchArgs);
                    it.lastLaunchTime = QDateTime::currentDateTime();
                }
            }
        } else {
            it.restartCount = 0;
            it.guardStartTime = QDateTime();
            logOperation(u"关闭守护"_s, programId(it.processName, it.launchArgs));
            if (!it.restartRulesActive) {
                if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText(u"未守护"_s);
            }
            if (tableWidget->item(displayRow, 3)) tableWidget->item(displayRow, 3)->setText("-");
            if (tableWidget->item(displayRow, 5)) tableWidget->item(displayRow, 5)->setText("0");
            if (tableWidget->item(displayRow, 6)) tableWidget->item(displayRow, 6)->setText("-");
        }
        updateButtonStates(displayRow);
        saveSettings();
    });

    // Scheduled restart button
    connect(srBtn, &QPushButton::clicked, this, [this, itemId = item.id]() {
        int idx = findItemIndexById(itemId);
        if (idx < 0) return;
        int displayRow = findRowById(itemId);
        if (displayRow < 0) return;
        GuardItem& it = items[idx];
        if (it.restartRulesActive) {
            it.restartRulesActive = false;
            logOperation(u"关闭定时重启"_s, programId(it.processName, it.launchArgs));
            QPushButton* b = qobject_cast<QPushButton*>(sender());
            if (b) b->setText(u"开启定时重启"_s);
            if (tableWidget->item(displayRow, 7)) tableWidget->item(displayRow, 7)->setText("-");
            if (tableWidget->item(displayRow, 8)) tableWidget->item(displayRow, 8)->setText("-");
            updateButtonStates(displayRow);
            saveSettings();
        } else {
            contextSetScheduleRules(QList<int>{displayRow}, false, true);
        }
    });

    // Scheduled run button
    connect(runBtn, &QPushButton::clicked, this, [this, itemId = item.id]() {
        int idx = findItemIndexById(itemId);
        if (idx < 0) return;
        int displayRow = findRowById(itemId);
        if (displayRow < 0) return;
        GuardItem& it = items[idx];
        if (it.scheduledRunEnabled) {
            it.scheduledRunEnabled = false;
            logOperation(u"关闭定时运行"_s, programId(it.processName, it.launchArgs));
            QPushButton* b = qobject_cast<QPushButton*>(sender());
            if (b) b->setText(u"开启定时运行"_s);
            if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText(u"未守护"_s);
            if (tableWidget->item(displayRow, 9)) tableWidget->item(displayRow, 9)->setText(formatStartDelay(it.startDelaySecs));
            updateButtonStates(displayRow);
            saveSettings();
        } else {
            contextSetScheduleRules(QList<int>{displayRow}, true, true);
        }
    });

    if (tableWidget->selectionModel())
        tableWidget->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);

    updateButtonStates(row);
}

void SuperGuardian::updateButtonStates(int row) {
    QWidget* opw = tableWidget->cellWidget(row, 10);
    if (!opw) return;
    QString id = rowId(row);
    int idx = findItemIndexById(id);
    if (idx < 0) return;
    const GuardItem& it = items[idx];

    QPushButton* guardBtn = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(id));
    QPushButton* srBtn = opw->findChild<QPushButton*>(QString("srBtn_%1").arg(id));
    QPushButton* runBtn = opw->findChild<QPushButton*>(QString("runBtn_%1").arg(id));

    bool guardOrRestartActive = it.guarding || it.restartRulesActive;
    bool runActive = it.scheduledRunEnabled;

    if (guardBtn) {
        const bool enabled = !runActive;
        guardBtn->setEnabled(enabled);
        guardBtn->setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);
    }
    if (srBtn) {
        const bool enabled = !runActive;
        srBtn->setEnabled(enabled);
        srBtn->setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);
    }
    if (runBtn) {
        const bool enabled = !guardOrRestartActive;
        runBtn->setEnabled(enabled);
        runBtn->setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);
    }
    requestResetColumnWidths();
}
