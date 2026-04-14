#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "EmailService.h"
#include "LogDatabase.h"
#include <QtWidgets>

namespace {

struct OpenLocationChoice {
    QString label;
    QString path;
};

static QString resolveExistingPath(const QString& token, const QString& baseDir) {
    QString value = token.trimmed();
    if (value.isEmpty())
        return QString();
    QFileInfo directInfo(value);
    if (directInfo.exists())
        return directInfo.absoluteFilePath();
    if (!baseDir.isEmpty() && QDir::isRelativePath(value)) {
        QFileInfo relativeInfo(QDir(baseDir).filePath(value));
        if (relativeInfo.exists())
            return relativeInfo.absoluteFilePath();
    }
    const int eqPos = value.indexOf('=');
    if (eqPos > 0 && eqPos + 1 < value.size())
        return resolveExistingPath(value.mid(eqPos + 1), baseDir);
    return QString();
}

static QList<OpenLocationChoice> collectOpenLocationChoices(const GuardItem& item) {
    QList<OpenLocationChoice> choices;
    QSet<QString> seenPaths;
    auto addChoice = [&](const QString& label, const QString& path) {
        const QString normalized = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        if (normalized.isEmpty() || seenPaths.contains(normalized)) return;
        seenPaths.insert(normalized);
        choices.append({ label, normalized });
    };
    addChoice(u"程序路径"_s, item.targetPath);
    if (!item.launchArgs.trimmed().isEmpty()) {
        QStringList parts = QProcess::splitCommand(u"dummy "_s + item.launchArgs);
        if (!parts.isEmpty()) parts.removeFirst();
        const QString baseDir = QFileInfo(item.targetPath).absolutePath();
        for (const QString& part : parts) {
            const QString resolved = resolveExistingPath(part, baseDir);
            if (resolved.isEmpty()) continue;
            const QFileInfo info(resolved);
            const QString displayPath = QDir::toNativeSeparators(resolved);
            addChoice(info.isDir() ? u"参数路径（文件夹）：%1"_s.arg(displayPath)
                : u"参数路径（文件）：%1"_s.arg(displayPath), resolved);
        }
    }
    return choices;
}

static void openPathInExplorer(const QString& path) {
    QFileInfo info(path);
    if (info.isDir()) {
        QProcess::startDetached("explorer.exe", QStringList() << QDir::toNativeSeparators(info.absoluteFilePath()));
        return;
    }

    QProcess::startDetached("explorer.exe", QStringList() << "/select," << QDir::toNativeSeparators(info.absoluteFilePath()));
}

}

// ---- 右键菜单与表格行操作 ----

void SuperGuardian::onTableContextMenuRequested(const QPoint& pos) {
    QModelIndex idx = tableWidget->indexAt(pos);

    auto doPaste = [this]() {
        GuardItem newItem = copiedItem;
        newItem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        newItem.guarding = false;
        newItem.restartRulesActive = false;
        newItem.scheduledRunEnabled = false;
        newItem.restartCount = 0;
        newItem.guardStartTime = QDateTime();
        newItem.startTime = QDateTime();
        newItem.lastRestart = QDateTime();
        newItem.lastLaunchTime = QDateTime();
        newItem.lastGuardRestartTime = QDateTime();
        newItem.startDelayExitTime = QDateTime();
        newItem.retryActive = false;
        newItem.currentRetryCount = 0;
        newItem.notifiedStartFailed = false;
        newItem.notifiedRestartFailed = false;
        newItem.notifiedRunFailed = false;
        newItem.notifiedRetryExhausted = false;
        int maxOrder = 0;
        for (const auto& it : items) maxOrder = qMax(maxOrder, it.insertionOrder);
        newItem.insertionOrder = maxOrder + 1;
        items.append(newItem);
        logOperation(u"粘贴程序"_s, programId(newItem.processName, newItem.launchArgs));
        rebuildTableFromItems();
        saveSettings();
    };

    if (!idx.isValid()) {
        if (!hasCopiedItem) return;
        QMenu menu(this);
        menu.addAction(u"粘贴"_s, this, doPaste);
        menu.exec(tableWidget->viewport()->mapToGlobal(pos));
        return;
    }

    int row = idx.row();
    int itemIndex = findItemIndexById(rowId(row));
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
            int selectedIndex = findItemIndexById(rowId(index.row()));
            if (selectedIndex >= 0) {
                targetRows.append(index.row());
            }
        }
    } else {
        targetRows.append(row);
    }
    std::sort(targetRows.begin(), targetRows.end());
    targetRows.erase(std::unique(targetRows.begin(), targetRows.end()), targetRows.end());

    // 检查功能状态
    bool anyScheduledRun = false;
    bool anyGuardOrRestart = false;
    bool anyActive = false;
    for (int r : targetRows) {
        int ii = findItemIndexById(rowId(r));
        if (ii >= 0) {
            if (items[ii].scheduledRunEnabled) anyScheduledRun = true;
            if (items[ii].guarding || items[ii].restartRulesActive) anyGuardOrRestart = true;
            if (items[ii].guarding || items[ii].restartRulesActive || items[ii].scheduledRunEnabled) anyActive = true;
        }
    }

    QMenu menu(this);

    // 备注
    menu.addAction(u"备注"_s, this, [this, targetRows]() { contextSetNote(targetRows); });
    // 置顶
    bool allPinned = true;
    for (int r : targetRows) { int ii = findItemIndexById(rowId(r)); if (ii >= 0 && !items[ii].pinned) { allPinned = false; break; } }
    menu.addAction(allPinned ? u"取消置顶"_s : u"置顶"_s,
        this, [this, targetRows]() { contextTogglePin(targetRows); });

    menu.addSeparator();

    // 定时重启规则（定时运行模式下隐藏）
    if (!anyScheduledRun)
        menu.addAction(u"定时重启规则"_s, this, [this, targetRows]() { contextSetScheduleRules(targetRows, false); });
    // 设置启动延时（定时运行模式下隐藏）
    if (!anyScheduledRun)
        menu.addAction(u"设置启动延时"_s, this, [this, targetRows]() { contextSetStartDelay(targetRows); });
    // 定时运行规则（守护或定时重启时隐藏）
    if (!anyGuardOrRestart)
        menu.addAction(u"定时运行规则"_s, this, [this, targetRows]() { contextSetScheduleRules(targetRows, true); });

    menu.addSeparator();

    // 手动启动
    menu.addAction(u"手动启动"_s, this, [this, targetRows]() { for (int row : targetRows) contextStartProgram(row); });
    // 终止进程
    menu.addAction(u"终止进程"_s, this, [this, targetRows]() {
        QString name = targetRows.size() == 1 && tableWidget->item(targetRows[0], 1)
            ? tableWidget->item(targetRows[0], 1)->text() : QString();
        QString msg = targetRows.size() == 1
            ? u"确认终止【%1】的进程吗？"_s.arg(name)
            : u"确认终止选中的 %1 个程序的进程吗？"_s.arg(targetRows.size());
        if (!showMessageDialog(this, u"终止进程"_s, msg, true)) return;
        for (int row : targetRows) contextKillProgram(row);
    });
    // 重试设置
    menu.addAction(u"重试设置"_s, this, [this, targetRows]() { contextSetRetryConfig(targetRows); });
    // 邮件提醒设置
    menu.addAction(u"邮件提醒设置"_s, this, [this, targetRows]() { contextSetEmailNotify(targetRows); });

    menu.addSeparator();

    // 复制（单选时可用）
    if (targetRows.size() == 1) {
        menu.addAction(u"复制"_s, this, [this, targetRows]() {
            int ii = findItemIndexById(rowId(targetRows[0]));
            if (ii >= 0) {
                copiedItem = items[ii];
                hasCopiedItem = true;
            }
        });
    }
    // 粘贴
    if (hasCopiedItem)
        menu.addAction(u"粘贴"_s, this, doPaste);

    menu.addSeparator();

    // 打开文件所在位置（单选时可用）
    if (targetRows.size() == 1)
        menu.addAction(u"打开文件所在的位置"_s, this, [this, row]() { contextOpenFileLocation(row); });
    // 移除项（有活跃功能时隐藏）
    if (!anyActive) {
        menu.addAction(u"移除项"_s, this, [this, targetRows]() {
            QString name = targetRows.size() == 1 && tableWidget->item(targetRows[0], 1)
                ? tableWidget->item(targetRows[0], 1)->text() : QString();
            QString msg = targetRows.size() == 1
                ? u"确认移除【%1】吗？"_s.arg(name)
                : u"确认移除选中的 %1 个程序项吗？"_s.arg(targetRows.size());
            if (!showMessageDialog(this, u"移除项"_s, msg, true)) return;
            QList<int> rows = targetRows;
            std::sort(rows.begin(), rows.end(), std::greater<int>());
            for (int row : rows) contextRemoveItem(row);
        });
    }

    menu.exec(tableWidget->viewport()->mapToGlobal(pos));
}

void SuperGuardian::contextStartProgram(int row) {
    int idx = findItemIndexById(rowId(row));
    if (idx < 0) return;
    launchProgram(items[idx].targetPath, items[idx].launchArgs);
    logOperation(u"手动启动"_s, programId(items[idx].processName, items[idx].launchArgs));
}

void SuperGuardian::contextKillProgram(int row) {
    int idx = findItemIndexById(rowId(row));
    if (idx < 0) return;
    killProcessesByName(items[idx].processName);
    logOperation(u"终止进程"_s, programId(items[idx].processName, items[idx].launchArgs));
}

void SuperGuardian::contextToggleGuard(int row) {
    int idx = findItemIndexById(rowId(row));
    if (idx < 0) return;
    items[idx].guarding = !items[idx].guarding;
    QWidget* opw = tableWidget->cellWidget(row, 10);
    if (opw) {
        QPushButton* b = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(items[idx].id));
        if (b) b->setText(items[idx].guarding ? u"关闭守护"_s : u"开始守护"_s);
    }
    if (items[idx].guarding) {
        items[idx].startTime = QDateTime::currentDateTime();
        items[idx].guardStartTime = QDateTime::currentDateTime();
        logOperation(u"开始守护"_s, programId(items[idx].processName, items[idx].launchArgs));
        int count = 0;
        bool running = isProcessRunning(items[idx].processName, count);
        if (!running && count == 0) {
            launchProgram(items[idx].targetPath, items[idx].launchArgs);
            items[idx].lastLaunchTime = QDateTime::currentDateTime();
        }
    } else {
        items[idx].restartCount = 0;
        items[idx].guardStartTime = QDateTime();
        logOperation(u"关闭守护"_s, programId(items[idx].processName, items[idx].launchArgs));
        int displayRow = findRowById(items[idx].id);
        if (displayRow >= 0) {
            if (!items[idx].restartRulesActive) {
                if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText(u"未守护"_s);
            }
            if (tableWidget->item(displayRow, 3)) tableWidget->item(displayRow, 3)->setText("-");
            if (tableWidget->item(displayRow, 5)) tableWidget->item(displayRow, 5)->setText("0");
            if (tableWidget->item(displayRow, 6)) tableWidget->item(displayRow, 6)->setText("-");
        }
    }
    updateButtonStates(row);
    saveSettings();
}

void SuperGuardian::contextRemoveItem(int row) {
    int idx = findItemIndexById(rowId(row));
    if (idx < 0) return;
    logOperation(u"移除项"_s, programId(items[idx].processName, items[idx].launchArgs));
    items.removeAt(idx);
    tableWidget->removeRow(row);
    saveSettings();
}

// ---- 双击打开设置启动程序/参数 ----

void SuperGuardian::onTableDoubleClicked(int row, int col) {
    Q_UNUSED(col);
    int idx = findItemIndexById(rowId(row));
    if (idx < 0) return;
    contextSetLaunchArgs(QList<int>{row});
}

void SuperGuardian::contextOpenFileLocation(int row) {
    int idx = findItemIndexById(rowId(row));
    if (idx < 0) return;

    const QList<OpenLocationChoice> choices = collectOpenLocationChoices(items[idx]);
    if (choices.isEmpty())
        return;

    QString targetPath = choices.first().path;
    if (choices.size() > 1) {
        QStringList labels;
        for (const OpenLocationChoice& choice : choices)
            labels.append(choice.label);

        bool ok = false;
        QString selected = showItemDialog(this, u"打开文件所在的位置"_s,
            u"检测到启动参数中包含文件路径，请选择要打开的位置："_s, labels, &ok);
        if (!ok)
            return;

        for (const OpenLocationChoice& choice : choices) {
            if (choice.label == selected) {
                targetPath = choice.path;
                break;
            }
        }
    }

    openPathInExplorer(targetPath);
}

// ---- 置顶操作 ----

void SuperGuardian::contextTogglePin(const QList<int>& rows) {
    bool allPinned = true;
    for (int r : rows) {
        int ii = findItemIndexById(rowId(r));
        if (ii >= 0 && !items[ii].pinned) { allPinned = false; break; }
    }
    for (int r : rows) {
        int ii = findItemIndexById(rowId(r));
        if (ii >= 0) {
            items[ii].pinned = !allPinned;
            logOperation(allPinned ? u"取消置顶"_s : u"置顶"_s, programId(items[ii].processName, items[ii].launchArgs));
        }
    }
    rebuildTableFromItems();
    saveSettings();
}
