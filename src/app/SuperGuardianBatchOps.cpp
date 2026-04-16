#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QDir>

namespace {

void setCellText(QTableWidget* tableWidget, int row, int column, const QString& text) {
    if (QTableWidgetItem* cell = tableWidget->item(row, column))
        cell->setText(text);
}

void setOperationButtonText(QTableWidget* tableWidget, int row, const QString& objectName, const QString& text) {
    if (QWidget* opw = tableWidget->cellWidget(row, 10)) {
        if (QPushButton* button = opw->findChild<QPushButton*>(objectName))
            button->setText(text);
    }
}

}

// ---- 行拖动与批量操作 ----

void SuperGuardian::handleRowMoved(int fromRow, int toRow) {
    handleRowsMoved(QList<int>{fromRow}, (toRow > fromRow) ? toRow + 1 : toRow);
}

void SuperGuardian::handleRowsMoved(const QList<int>& rows, int insertBefore) {
    QStringList idOrder;
    for (int r = 0; r < tableWidget->rowCount(); r++) {
        QTableWidgetItem* it = tableWidget->item(r, 0);
        if (!it) continue;
        idOrder << it->data(Qt::UserRole).toString();
    }

    QStringList movedIds;
    QSet<int> movedSet(rows.begin(), rows.end());
    for (int r : rows) {
        if (r >= 0 && r < idOrder.size())
            movedIds << idOrder[r];
    }
    if (movedIds.isEmpty()) return;

    QStringList remaining;
    for (int r = 0; r < idOrder.size(); r++) {
        if (!movedSet.contains(r))
            remaining << idOrder[r];
    }
    int adjustedInsert = 0;
    for (int r = 0; r < insertBefore && r < idOrder.size(); r++) {
        if (!movedSet.contains(r))
            adjustedInsert++;
    }
    if (adjustedInsert > remaining.size()) adjustedInsert = remaining.size();

    for (int i = 0; i < movedIds.size(); i++)
        remaining.insert(adjustedInsert + i, movedIds[i]);

    QVector<GuardItem> newItems;
    for (const QString& p : remaining) {
        int idx = findItemIndexById(p);
        if (idx >= 0) newItems.append(items[idx]);
    }
    items = newItems;
    for (int i = 0; i < items.size(); i++)
        items[i].insertionOrder = i;
    rebuildTableFromItems();

    tableWidget->clearSelection();
    for (const QString& p : movedIds) {
        int newRow = findRowById(p);
        if (newRow >= 0) {
            QModelIndex idx = tableWidget->model()->index(newRow, 0);
            tableWidget->selectionModel()->select(idx,
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    saveSettings();
}

void SuperGuardian::closeAllGuards() {
    if (!showMessageDialog(this, u"关闭所有守护"_s,
        u"确认关闭所有程序的守护吗？"_s, true))
        return;
    logOperation(u"关闭所有守护"_s);
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].guarding) {
            items[i].guarding = false;
            items[i].restartCount = 0;
            items[i].guardStartTime = QDateTime();
            int row = findRowById(items[i].id);
            if (row >= 0) {
                setOperationButtonText(tableWidget, row, QString("guardBtn_%1").arg(items[i].id), u"开始守护"_s);
                if (!items[i].restartRulesActive)
                    setCellText(tableWidget, row, 2, u"未守护"_s);
                setCellText(tableWidget, row, 3, u"-"_s);
                setCellText(tableWidget, row, 5, u"0"_s);
                setCellText(tableWidget, row, 6, u"-"_s);
                updateButtonStates(row);
            }
        }
    }
    saveSettings();
}

void SuperGuardian::closeAllScheduledRestart() {
    if (!showMessageDialog(this, u"关闭所有定时重启"_s,
        u"确认关闭所有程序的定时重启吗？"_s, true))
        return;
    logOperation(u"关闭所有定时重启"_s);
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].restartRulesActive) {
            items[i].restartRulesActive = false;
            int row = findRowById(items[i].id);
            if (row >= 0) {
                setOperationButtonText(tableWidget, row, QString("srBtn_%1").arg(items[i].id), u"开启定时重启"_s);
                setCellText(tableWidget, row, 7, u"-"_s);
                setCellText(tableWidget, row, 8, u"-"_s);
                updateButtonStates(row);
            }
        }
    }
    saveSettings();
}

void SuperGuardian::closeAllScheduledRun() {
    if (!showMessageDialog(this, u"关闭所有定时运行"_s,
        u"确认关闭所有程序的定时运行吗？"_s, true))
        return;
    logOperation(u"关闭所有定时运行"_s);
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].scheduledRunEnabled) {
            items[i].scheduledRunEnabled = false;
            int row = findRowById(items[i].id);
            if (row >= 0) {
                setOperationButtonText(tableWidget, row, QString("runBtn_%1").arg(items[i].id), u"开启定时运行"_s);
                setCellText(tableWidget, row, 2, u"未守护"_s);
                setCellText(tableWidget, row, 9, formatStartDelay(items[i].startDelaySecs));
                updateButtonStates(row);
            }
        }
    }
    saveSettings();
}

void SuperGuardian::closeAllOperations() {
    if (!showMessageDialog(this, u"关闭所有操作项"_s,
        u"确认关闭所有程序的守护、定时重启和定时运行吗？\n此操作不可撤销。"_s, true))
        return;
    logOperation(u"关闭所有操作项"_s);
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].guarding) {
            items[i].guarding = false;
            items[i].restartCount = 0;
            items[i].guardStartTime = QDateTime();
            int row = findRowById(items[i].id);
            if (row >= 0) {
                setOperationButtonText(tableWidget, row, QString("guardBtn_%1").arg(items[i].id), u"开始守护"_s);
                if (!items[i].restartRulesActive)
                    setCellText(tableWidget, row, 2, u"未守护"_s);
                setCellText(tableWidget, row, 3, u"-"_s);
                setCellText(tableWidget, row, 5, u"0"_s);
                setCellText(tableWidget, row, 6, u"-"_s);
            }
        }
        if (items[i].restartRulesActive) {
            items[i].restartRulesActive = false;
            int row = findRowById(items[i].id);
            if (row >= 0) {
                setOperationButtonText(tableWidget, row, QString("srBtn_%1").arg(items[i].id), u"开启定时重启"_s);
                setCellText(tableWidget, row, 7, u"-"_s);
                setCellText(tableWidget, row, 8, u"-"_s);
            }
        }
        if (items[i].scheduledRunEnabled) {
            items[i].scheduledRunEnabled = false;
            int row = findRowById(items[i].id);
            if (row >= 0) {
                setOperationButtonText(tableWidget, row, QString("runBtn_%1").arg(items[i].id), u"开启定时运行"_s);
                setCellText(tableWidget, row, 2, u"未守护"_s);
                setCellText(tableWidget, row, 9, formatStartDelay(items[i].startDelaySecs));
            }
        }
        if (int row = findRowById(items[i].id); row >= 0)
            updateButtonStates(row);
    }
    saveSettings();
}

// ---- 程序操作 ----

namespace {

QString normalizeWindowsPath(const QString& path) {
    QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return trimmed;
    return QDir::toNativeSeparators(QDir::cleanPath(trimmed));
}

}

void SuperGuardian::parseAndAddFromInput() {
    QString text = lineEdit->text().trimmed();
    if (text.isEmpty()) return;
    QString progPath, progArgs;
    if (text.startsWith('"')) {
        int cq = text.indexOf('"', 1);
        if (cq > 0) { progPath = text.mid(1, cq - 1); progArgs = text.mid(cq + 1).trimmed(); }
        else progPath = text.mid(1);
    } else if (QFileInfo::exists(text)) {
        progPath = text;
    } else {
        bool found = false;
        int searchFrom = 0;
        while (searchFrom < text.length()) {
            int sp = text.indexOf(' ', searchFrom);
            if (sp < 0) break;
            QString cand = text.left(sp);
            if (QFileInfo::exists(cand) || !QStandardPaths::findExecutable(cand).isEmpty()) {
                progPath = cand; progArgs = text.mid(sp + 1).trimmed(); found = true; break;
            }
            searchFrom = sp + 1;
        }
        if (!found) progPath = text;
    }
    addProgram(progPath.trimmed(), progArgs.trimmed());
    lineEdit->clear();
}

void SuperGuardian::addProgram(const QString& path, const QString& extraArgs) {
    QString resolvedPath = normalizeWindowsPath(path);
    if (resolvedPath.isEmpty())
        return;

    QFileInfo fi(resolvedPath);
    bool isSystemTool = false;
    if (!fi.exists()) {
        QString found = QStandardPaths::findExecutable(path.trimmed());
        if (found.isEmpty()) return;
        resolvedPath = normalizeWindowsPath(found);
        isSystemTool = true;
    }
    if (!isSystemTool) {
        QString nameOnly = QFileInfo(resolvedPath).fileName();
        QString found = QStandardPaths::findExecutable(nameOnly);
        if (!found.isEmpty() && QFileInfo(found).canonicalFilePath() == QFileInfo(resolvedPath).canonicalFilePath())
            isSystemTool = true;
    }
    if (!isSystemTool && !duplicateWhitelist.contains(resolvedPath, Qt::CaseInsensitive)) {
        for (const GuardItem& it : items) if (it.path == resolvedPath) return;
    }

    GuardItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.path = resolvedPath;
    QString shortcutArgs;
    item.targetPath = normalizeWindowsPath(resolveShortcut(resolvedPath, &shortcutArgs));
    if (!extraArgs.isEmpty())
        item.launchArgs = shortcutArgs.isEmpty() ? extraArgs : (shortcutArgs + " " + extraArgs);
    else
        item.launchArgs = shortcutArgs;
    item.processName = QFileInfo(item.targetPath).fileName();
    item.guarding = false;
    int maxOrder = 0;
    for (const auto& it : items) maxOrder = qMax(maxOrder, it.insertionOrder);
    item.insertionOrder = maxOrder + 1;
    items.append(item);
    logOperation(u"\u6dfb\u52a0\u7a0b\u5e8f"_s, programId(item.processName, item.launchArgs));

    rebuildTableFromItems();
    saveSettings();
}

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

    QString subject = u"[SuperGuardian] %1 - %2"_s.arg(item.processName, event);
    sendNotificationAsync(smtpConfig, subject, detail);
}
