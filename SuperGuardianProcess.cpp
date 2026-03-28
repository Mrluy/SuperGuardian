#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include <QtWidgets>
#include <QThread>

// ---- 程序添加与进程监控 ----

void SuperGuardian::addProgram(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return;

    // avoid duplicates by exact path
    for (const GuardItem& it : items) if (it.path == path) return;

    GuardItem item;
    item.path = path;
    item.targetPath = resolveShortcut(path);
    item.processName = QFileInfo(item.targetPath).fileName();
    item.guarding = false;

    items.append(item);

    int row = tableWidget->rowCount();
    tableWidget->insertRow(row);

    auto makeItem = [&](const QString& t){ QTableWidgetItem* it = new QTableWidgetItem(t); it->setFlags(it->flags() & ~Qt::ItemIsEditable); it->setTextAlignment(Qt::AlignCenter); return it; };
    QTableWidgetItem* nameItem = new QTableWidgetItem(item.processName);
    nameItem->setIcon(getFileIcon(item.targetPath));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole, item.path);
    nameItem->setToolTip(item.processName);
    tableWidget->setItem(row, 0, nameItem);
    tableWidget->setItem(row, 1, makeItem(QString::fromUtf8("未守护")));
    tableWidget->setItem(row, 2, makeItem("-"));
    tableWidget->setItem(row, 3, makeItem("-"));
    tableWidget->setItem(row, 4, makeItem("0"));
    tableWidget->setItem(row, 5, makeItem("-"));
    tableWidget->setItem(row, 6, makeItem("-"));

    // 操作列: 守护按钮 + 定时重启按钮
    QWidget* opWidget = new QWidget();
    QHBoxLayout* opLay = new QHBoxLayout(opWidget);
    opLay->setContentsMargins(2,0,2,0);
    opLay->setSpacing(4);
    QPushButton* btn = new QPushButton(QString::fromUtf8("开始守护"));
    QPushButton* srBtn = new QPushButton(QString::fromUtf8("开启定时重启"));
    srBtn->setObjectName(QString("srBtn_%1").arg(item.path));
    opLay->addWidget(btn);
    opLay->addWidget(srBtn);
    tableWidget->setCellWidget(row, 7, opWidget);
    // ensure no cell is left as current editor (prevents caret showing)
    if (tableWidget->selectionModel()) tableWidget->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    connect(btn, &QPushButton::clicked, this, [this, itemPath=item.path]() {
        int idx = findItemIndexByPath(itemPath);
        if (idx < 0) return;
        int displayRow = findRowByPath(itemPath);
        if (displayRow < 0) return;
        GuardItem& it = items[idx];
        it.guarding = !it.guarding;
        QPushButton* b = qobject_cast<QPushButton*>(sender());
        if (b) b->setText(it.guarding ? QString::fromUtf8("关闭守护") : QString::fromUtf8("开始守护"));
        if (it.guarding) {
            it.startTime = QDateTime::currentDateTime();
            int count = 0;
            bool running = isProcessRunning(it.processName, count);
            if (!running && count == 0) {
                launchProgram(it.path);
                it.lastLaunchTime = QDateTime::currentDateTime();
            }
        } else {
            if (it.scheduledRestartIntervalSecs <= 0) {
                if (tableWidget->item(displayRow, 1)) tableWidget->item(displayRow, 1)->setText(QString::fromUtf8("未守护"));
            }
            if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText("-");
        }
    });
    connect(srBtn, &QPushButton::clicked, this, [this, itemPath=item.path]() {
        int idx = findItemIndexByPath(itemPath);
        if (idx < 0) return;
        int displayRow = findRowByPath(itemPath);
        if (displayRow < 0) return;
        GuardItem& it = items[idx];
        if (it.scheduledRestartIntervalSecs > 0) {
            // 停止定时重启
            it.scheduledRestartIntervalSecs = 0;
            it.nextScheduledRestart = QDateTime();
        } else {
            // 弹出设置对话框
            contextSetScheduledRestart(QList<int>{displayRow});
            return;
        }
        QPushButton* b = qobject_cast<QPushButton*>(sender());
        if (b) b->setText(QString::fromUtf8("开启定时重启"));
        if (tableWidget->item(displayRow, 5)) tableWidget->item(displayRow, 5)->setText("-");
        if (tableWidget->item(displayRow, 6)) tableWidget->item(displayRow, 6)->setText("-");
        saveSettings();
    });
}

void SuperGuardian::checkProcesses() {
    for (int row = 0; row < tableWidget->rowCount(); ++row) {
        int idx = findItemIndexByPath(rowPath(row));
        if (idx < 0) continue;
        GuardItem& item = items[idx];

        bool hasGuard = item.guarding;
        bool hasScheduledRestart = item.scheduledRestartIntervalSecs > 0;
        if (!hasGuard && !hasScheduledRestart) continue;

        int count = 0;
        bool running = isProcessRunning(item.processName, count);
        bool scheduledRestarted = false;

        // 定时重启逻辑（独立于守护）
        if (hasScheduledRestart && item.nextScheduledRestart.isValid()
            && QDateTime::currentDateTime() >= item.nextScheduledRestart) {
            bool cooldown = item.lastGuardRestartTime.isValid()
                && item.lastGuardRestartTime.secsTo(QDateTime::currentDateTime()) < 30;
            if (!cooldown) {
                if (running) {
                    killProcessesByName(item.processName);
                    QThread::msleep(500);
                    launchProgram(item.path);
                    item.lastLaunchTime = QDateTime::currentDateTime();
                    item.lastRestart = QDateTime::currentDateTime();
                    scheduledRestarted = true;
                    running = true;
                }
                item.nextScheduledRestart = QDateTime::currentDateTime().addSecs(item.scheduledRestartIntervalSecs);
            }
        }

        // 守护逻辑（仅当守护开启时）
        if (hasGuard && !running && !scheduledRestarted) {
            if (item.lastLaunchTime.isValid() && item.lastLaunchTime.secsTo(QDateTime::currentDateTime()) < 10) {
                // 刚启动过，跳过启动但仍更新UI
            } else {
                int recount = 0;
                isProcessRunning(item.processName, recount);
                if (recount == 0) {
                    launchProgram(item.path);
                    item.lastLaunchTime = QDateTime::currentDateTime();
                    item.lastGuardRestartTime = QDateTime::currentDateTime();
                    item.restartCount++;
                    item.lastRestart = QDateTime::currentDateTime();
                    running = true;
                }
            }
        }

        // 更新UI
        if (hasGuard) {
            if (tableWidget->item(row,1)) tableWidget->item(row,1)->setText(running ? QString::fromUtf8("运行中") : QString::fromUtf8("已重启"));
            qint64 secs = item.startTime.isValid() ? item.startTime.secsTo(QDateTime::currentDateTime()) : 0;
            if (tableWidget->item(row,2)) tableWidget->item(row,2)->setText([&](qint64 s)->QString{
                qint64 days = s / 86400;
                qint64 hours = (s % 86400) / 3600;
                qint64 mins = (s % 3600) / 60;
                if (days > 0) return QString::number(days) + QString::fromUtf8("天") + QString::number(hours) + QString::fromUtf8("时") + QString::number(mins) + QString::fromUtf8("分");
                if (hours > 0) return QString::number(hours) + QString::fromUtf8("时") + QString::number(mins) + QString::fromUtf8("分");
                return QString::number(mins) + QString::fromUtf8("分");
            }(secs));
        } else if (hasScheduledRestart) {
            if (tableWidget->item(row,1)) tableWidget->item(row,1)->setText(running ? QString::fromUtf8("运行中") : QString::fromUtf8("未运行"));
            if (tableWidget->item(row,2)) tableWidget->item(row,2)->setText("-");
        }
        if (tableWidget->item(row,3)) tableWidget->item(row,3)->setText(item.lastRestart.isValid() ? item.lastRestart.toString(QString::fromUtf8("yyyy年M月d日 hh:mm:ss")) : "-");
        if (tableWidget->item(row,4)) tableWidget->item(row,4)->setText(QString::number(item.restartCount));
        if (tableWidget->item(row,5)) tableWidget->item(row,5)->setText(formatRestartInterval(item.scheduledRestartIntervalSecs));
        if (tableWidget->item(row,6)) tableWidget->item(row,6)->setText(item.nextScheduledRestart.isValid() ? item.nextScheduledRestart.toString(QString::fromUtf8("yyyy年M月d日 hh:mm:ss")) : "-");
    }
    saveSettings();
}
