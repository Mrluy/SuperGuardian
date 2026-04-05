#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "LogDatabase.h"
#include <QtWidgets>

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
