#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include <QtWidgets>

// ---- 行拖动与批量操作 ----

void SuperGuardian::handleRowMoved(int fromRow, int toRow) {
    handleRowsMoved(QList<int>{fromRow}, (toRow > fromRow) ? toRow + 1 : toRow);
}

void SuperGuardian::handleRowsMoved(const QList<int>& rows, int insertBefore) {
    QStringList pathOrder;
    for (int r = 0; r < tableWidget->rowCount(); r++) {
        QTableWidgetItem* it = tableWidget->item(r, 0);
        if (!it) continue;
        pathOrder << it->data(Qt::UserRole).toString();
    }

    QStringList movedPaths;
    QSet<int> movedSet(rows.begin(), rows.end());
    for (int r : rows) {
        if (r >= 0 && r < pathOrder.size())
            movedPaths << pathOrder[r];
    }
    if (movedPaths.isEmpty()) return;

    QStringList remaining;
    for (int r = 0; r < pathOrder.size(); r++) {
        if (!movedSet.contains(r))
            remaining << pathOrder[r];
    }
    int adjustedInsert = 0;
    for (int r = 0; r < insertBefore && r < pathOrder.size(); r++) {
        if (!movedSet.contains(r))
            adjustedInsert++;
    }
    if (adjustedInsert > remaining.size()) adjustedInsert = remaining.size();

    for (int i = 0; i < movedPaths.size(); i++)
        remaining.insert(adjustedInsert + i, movedPaths[i]);

    QVector<GuardItem> newItems;
    for (const QString& p : remaining) {
        int idx = findItemIndexByPath(p);
        if (idx >= 0) newItems.append(items[idx]);
    }
    items = newItems;
    for (int i = 0; i < items.size(); i++)
        items[i].insertionOrder = i;
    rebuildTableFromItems();

    tableWidget->clearSelection();
    for (const QString& p : movedPaths) {
        int newRow = findRowByPath(p);
        if (newRow >= 0) {
            for (int c = 0; c < tableWidget->columnCount(); c++) {
                QModelIndex idx = tableWidget->model()->index(newRow, c);
                tableWidget->selectionModel()->select(idx, QItemSelectionModel::Select);
            }
        }
    }
    saveSettings();
}

void SuperGuardian::closeAllGuards() {
    if (!showMessageDialog(this, u"关闭所有守护"_s,
        u"确认关闭所有程序的守护吗？"_s, true))
        return;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].guarding) {
            items[i].guarding = false;
            items[i].restartCount = 0;
            items[i].guardStartTime = QDateTime();
            int row = findRowByPath(items[i].path);
            if (row >= 0) {
                QWidget* opw = tableWidget->cellWidget(row, 9);
                if (opw) {
                    QPushButton* b = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(items[i].path));
                    if (b) b->setText(u"开始守护"_s);
                }
                if (!items[i].restartRulesActive) {
                    if (tableWidget->item(row, 1)) tableWidget->item(row, 1)->setText(u"未守护"_s);
                }
                if (tableWidget->item(row, 2)) tableWidget->item(row, 2)->setText("-");
                if (tableWidget->item(row, 4)) tableWidget->item(row, 4)->setText("0");
                if (tableWidget->item(row, 5)) tableWidget->item(row, 5)->setText("-");
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
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].restartRulesActive) {
            items[i].restartRulesActive = false;
            int row = findRowByPath(items[i].path);
            if (row >= 0) {
                QWidget* opw = tableWidget->cellWidget(row, 9);
                if (opw) {
                    QPushButton* b = opw->findChild<QPushButton*>(QString("srBtn_%1").arg(items[i].path));
                    if (b) b->setText(u"开启定时重启"_s);
                }
                if (tableWidget->item(row, 6)) tableWidget->item(row, 6)->setText("-");
                if (tableWidget->item(row, 7)) tableWidget->item(row, 7)->setText("-");
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
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].scheduledRunEnabled) {
            items[i].scheduledRunEnabled = false;
            int row = findRowByPath(items[i].path);
            if (row >= 0) {
                QWidget* opw = tableWidget->cellWidget(row, 9);
                if (opw) {
                    QPushButton* b = opw->findChild<QPushButton*>(QString("runBtn_%1").arg(items[i].path));
                    if (b) b->setText(u"开启定时运行"_s);
                }
                if (tableWidget->item(row, 1)) tableWidget->item(row, 1)->setText(u"未守护"_s);
                if (tableWidget->item(row, 8)) tableWidget->item(row, 8)->setText(formatStartDelay(items[i].startDelaySecs));
                updateButtonStates(row);
            }
        }
    }
    saveSettings();
}
