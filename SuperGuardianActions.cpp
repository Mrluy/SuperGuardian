#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "EmailService.h"
#include <QtWidgets>

// ---- 右键菜单与表格行操作 ----

void SuperGuardian::onTableContextMenuRequested(const QPoint& pos) {
    QModelIndex idx = tableWidget->indexAt(pos);
    if (!idx.isValid()) return;
    int row = idx.row();

    // --- Item context menu ---
    int itemIndex = findItemIndexByPath(rowPath(row));
    if (itemIndex < 0) return;

    QList<int> targetRows;
    const auto selectedRows = tableWidget->selectionModel() ? tableWidget->selectionModel()->selectedRows() : QModelIndexList{};
    bool clickedRowAlreadySelected = false;
    for (const QModelIndex& index : selectedRows) {
        if (index.row() == row) {
            clickedRowAlreadySelected = true;
            break;
        }
    }
    if (clickedRowAlreadySelected && !selectedRows.isEmpty()) {
        for (const QModelIndex& index : selectedRows) {
            int selectedIndex = findItemIndexByPath(rowPath(index.row()));
            if (selectedIndex >= 0) {
                targetRows.append(index.row());
            }
        }
    } else {
        targetRows.append(row);
    }
    std::sort(targetRows.begin(), targetRows.end());
    targetRows.erase(std::unique(targetRows.begin(), targetRows.end()), targetRows.end());

    QMenu menu(this);

    // Pin toggle
    bool allPinned = true;
    for (int r : targetRows) { int ii = findItemIndexByPath(rowPath(r)); if (ii >= 0 && !items[ii].pinned) { allPinned = false; break; } }
    menu.addAction(allPinned ? QString::fromUtf8("\u53d6\u6d88\u7f6e\u9876") : QString::fromUtf8("\u7f6e\u9876"),
        this, [this, targetRows]() { contextTogglePin(targetRows); });

    // 备注（最上方区域，紧跟置顶后）
    menu.addAction(QString::fromUtf8("\u5907\u6ce8"), this, [this, targetRows]() { contextSetNote(targetRows); });
    menu.addSeparator();

    // 检查是否有任一目标行处于定时运行模式或守护/定时重启模式
    bool anyScheduledRun = false;
    bool anyGuardOrRestart = false;
    bool anyActive = false;
    for (int r : targetRows) {
        int ii = findItemIndexByPath(rowPath(r));
        if (ii >= 0) {
            if (items[ii].scheduledRunEnabled) anyScheduledRun = true;
            if (items[ii].guarding || items[ii].restartRulesActive) anyGuardOrRestart = true;
            if (items[ii].guarding || items[ii].restartRulesActive || items[ii].scheduledRunEnabled) anyActive = true;
        }
    }

    menu.addAction(QString::fromUtf8("\u624b\u52a8\u542f\u52a8"), this, [this, targetRows]() { for (int row : targetRows) contextStartProgram(row); });
    menu.addAction(QString::fromUtf8("\u7ec8\u6b62\u8fdb\u7a0b"), this, [this, targetRows]() {
        QString name = targetRows.size() == 1 && tableWidget->item(targetRows[0], 0)
            ? tableWidget->item(targetRows[0], 0)->text() : QString();
        QString msg = targetRows.size() == 1
            ? QString::fromUtf8("\u786e\u8ba4\u7ec8\u6b62\u3010%1\u3011\u7684\u8fdb\u7a0b\u5417\uff1f").arg(name)
            : QString::fromUtf8("\u786e\u8ba4\u7ec8\u6b62\u9009\u4e2d\u7684 %1 \u4e2a\u7a0b\u5e8f\u7684\u8fdb\u7a0b\u5417\uff1f").arg(targetRows.size());
        if (!showMessageDialog(this, QString::fromUtf8("\u7ec8\u6b62\u8fdb\u7a0b"), msg, true)) return;
        for (int row : targetRows) contextKillProgram(row);
    });
    QAction* launchConfigAct = menu.addAction(QString::fromUtf8("\u8bbe\u7f6e\u542f\u52a8\u7a0b\u5e8f/\u53c2\u6570"), this, [this, targetRows]() { contextSetLaunchArgs(targetRows); });
    QAction* restartRulesAct = menu.addAction(QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u89c4\u5219"), this, [this, targetRows]() { contextSetScheduleRules(targetRows, false); });
    QAction* startDelayAct = menu.addAction(QString::fromUtf8("\u8bbe\u7f6e\u542f\u52a8\u5ef6\u65f6"), this, [this, targetRows]() { contextSetStartDelay(targetRows); });
    QAction* runRulesAct = menu.addAction(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c\u89c4\u5219"), this, [this, targetRows]() { contextSetScheduleRules(targetRows, true); });
    menu.addAction(QString::fromUtf8("\u91cd\u8bd5\u8bbe\u7f6e"), this, [this, targetRows]() { contextSetRetryConfig(targetRows); });
    menu.addAction(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192\u8bbe\u7f6e"), this, [this, targetRows]() { contextSetEmailNotify(targetRows); });

    // 定时运行时禁用定时重启规则和设置启动延时
    if (anyScheduledRun) {
        restartRulesAct->setEnabled(false);
        startDelayAct->setEnabled(false);
    }
    // 守护或定时重启时禁用定时运行规则
    if (anyGuardOrRestart) {
        runRulesAct->setEnabled(false);
    }

    menu.addSeparator();

    if (targetRows.size() == 1) {
        menu.addAction(QString::fromUtf8("\u6253\u5f00\u6587\u4ef6\u6240\u5728\u7684\u4f4d\u7f6e"), this, [this, row]() { contextOpenFileLocation(row); });
    }
    QAction* removeAct = menu.addAction(QString::fromUtf8("\u79fb\u9664\u9879"), this, [this, targetRows]() {
        QString name = targetRows.size() == 1 && tableWidget->item(targetRows[0], 0)
            ? tableWidget->item(targetRows[0], 0)->text() : QString();
        QString msg = targetRows.size() == 1
            ? QString::fromUtf8("\u786e\u8ba4\u79fb\u9664\u3010%1\u3011\u5417\uff1f").arg(name)
            : QString::fromUtf8("\u786e\u8ba4\u79fb\u9664\u9009\u4e2d\u7684 %1 \u4e2a\u7a0b\u5e8f\u9879\u5417\uff1f").arg(targetRows.size());
        if (!showMessageDialog(this, QString::fromUtf8("\u79fb\u9664\u9879"), msg, true)) return;
        QList<int> rows = targetRows;
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows) contextRemoveItem(row);
    });
    if (anyActive) removeAct->setEnabled(false);
    menu.exec(tableWidget->viewport()->mapToGlobal(pos));
}

void SuperGuardian::contextStartProgram(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    launchProgram(items[idx].targetPath, items[idx].launchArgs);
}

void SuperGuardian::contextKillProgram(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    killProcessesByName(items[idx].processName);
}

void SuperGuardian::contextToggleGuard(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    items[idx].guarding = !items[idx].guarding;
    QWidget* opw = tableWidget->cellWidget(row, 8);
    if (opw) {
        QPushButton* b = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(items[idx].path));
        if (b) b->setText(items[idx].guarding ? QString::fromUtf8("\u5173\u95ed\u5b88\u62a4") : QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
    }
    if (items[idx].guarding) {
        items[idx].startTime = QDateTime::currentDateTime();
        int count = 0;
        bool running = isProcessRunning(items[idx].processName, count);
        if (!running && count == 0) {
            launchProgram(items[idx].targetPath, items[idx].launchArgs);
            items[idx].lastLaunchTime = QDateTime::currentDateTime();
        }
    } else {
        items[idx].restartCount = 0;
        int displayRow = findRowByPath(items[idx].path);
        if (displayRow >= 0) {
            if (!items[idx].restartRulesActive) {
                if (tableWidget->item(displayRow, 1)) tableWidget->item(displayRow, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
            }
            if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText("-");
            if (tableWidget->item(displayRow, 4)) tableWidget->item(displayRow, 4)->setText("0");
        }
    }
    updateButtonStates(row);
    saveSettings();
}

void SuperGuardian::contextRemoveItem(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    items.removeAt(idx);
    tableWidget->removeRow(row);
    saveSettings();
}

// ---- 双击打开设置启动程序/参数 ----

void SuperGuardian::onTableDoubleClicked(int row, int col) {
    Q_UNUSED(col);
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    contextSetLaunchArgs(QList<int>{row});
}

void SuperGuardian::contextOpenFileLocation(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    QString filePath = QDir::toNativeSeparators(items[idx].targetPath);
    QProcess::startDetached("explorer.exe", QStringList() << "/select," << filePath);
}

// ---- 置顶操作 ----

void SuperGuardian::contextTogglePin(const QList<int>& rows) {
    bool allPinned = true;
    for (int r : rows) {
        int ii = findItemIndexByPath(rowPath(r));
        if (ii >= 0 && !items[ii].pinned) { allPinned = false; break; }
    }
    for (int r : rows) {
        int ii = findItemIndexByPath(rowPath(r));
        if (ii >= 0) items[ii].pinned = !allPinned;
    }
    rebuildTableFromItems();
    saveSettings();
}

// ---- 行拖动与批量操作 ----

void SuperGuardian::handleRowMoved(int fromRow, int toRow) {
    QStringList pathOrder;
    for (int r = 0; r < tableWidget->rowCount(); r++) {
        QTableWidgetItem* it = tableWidget->item(r, 0);
        if (!it) continue;
        pathOrder << it->data(Qt::UserRole).toString();
    }
    if (fromRow < 0 || fromRow >= pathOrder.size()) return;
    QString movedPath = pathOrder.takeAt(fromRow);
    pathOrder.insert(toRow, movedPath);

    QVector<GuardItem> newItems;
    for (const QString& p : pathOrder) {
        int idx = findItemIndexByPath(p);
        if (idx >= 0) newItems.append(items[idx]);
    }
    items = newItems;
    rebuildTableFromItems();
    int newRow = findRowByPath(movedPath);
    if (newRow >= 0) tableWidget->selectRow(newRow);
    saveSettings();
}

void SuperGuardian::closeAllGuards() {
    if (!showMessageDialog(this, QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b88\u62a4"),
        QString::fromUtf8("\u786e\u8ba4\u5173\u95ed\u6240\u6709\u7a0b\u5e8f\u7684\u5b88\u62a4\u5417\uff1f"), true))
        return;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].guarding) {
            items[i].guarding = false;
            items[i].restartCount = 0;
            int row = findRowByPath(items[i].path);
            if (row >= 0) {
                QWidget* opw = tableWidget->cellWidget(row, 8);
                if (opw) {
                    QPushButton* b = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(items[i].path));
                    if (b) b->setText(QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
                }
                if (!items[i].restartRulesActive) {
                    if (tableWidget->item(row, 1)) tableWidget->item(row, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
                }
                if (tableWidget->item(row, 2)) tableWidget->item(row, 2)->setText("-");
                if (tableWidget->item(row, 4)) tableWidget->item(row, 4)->setText("0");
                updateButtonStates(row);
            }
        }
    }
    saveSettings();
}

void SuperGuardian::closeAllScheduledRestart() {
    if (!showMessageDialog(this, QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b9a\u65f6\u91cd\u542f"),
        QString::fromUtf8("\u786e\u8ba4\u5173\u95ed\u6240\u6709\u7a0b\u5e8f\u7684\u5b9a\u65f6\u91cd\u542f\u5417\uff1f"), true))
        return;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].restartRulesActive) {
            items[i].restartRulesActive = false;
            int row = findRowByPath(items[i].path);
            if (row >= 0) {
                QWidget* opw = tableWidget->cellWidget(row, 8);
                if (opw) {
                    QPushButton* b = opw->findChild<QPushButton*>(QString("srBtn_%1").arg(items[i].path));
                    if (b) b->setText(QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u91cd\u542f"));
                }
                if (tableWidget->item(row, 5)) tableWidget->item(row, 5)->setText("-");
                if (tableWidget->item(row, 6)) tableWidget->item(row, 6)->setText("-");
                updateButtonStates(row);
            }
        }
    }
    saveSettings();
}

void SuperGuardian::closeAllScheduledRun() {
    if (!showMessageDialog(this, QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b9a\u65f6\u8fd0\u884c"),
        QString::fromUtf8("\u786e\u8ba4\u5173\u95ed\u6240\u6709\u7a0b\u5e8f\u7684\u5b9a\u65f6\u8fd0\u884c\u5417\uff1f"), true))
        return;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].scheduledRunEnabled) {
            items[i].scheduledRunEnabled = false;
            int row = findRowByPath(items[i].path);
            if (row >= 0) {
                QWidget* opw = tableWidget->cellWidget(row, 8);
                if (opw) {
                    QPushButton* b = opw->findChild<QPushButton*>(QString("runBtn_%1").arg(items[i].path));
                    if (b) b->setText(QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u8fd0\u884c"));
                }
                if (tableWidget->item(row, 1)) tableWidget->item(row, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
                if (tableWidget->item(row, 7)) tableWidget->item(row, 7)->setText(formatStartDelay(items[i].startDelaySecs));
                updateButtonStates(row);
            }
        }
    }
    saveSettings();
}
