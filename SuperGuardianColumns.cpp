#include "SuperGuardian.h"
#include "AppStorage.h"
#include <QtWidgets>

// ---- Column width management ----

void SuperGuardian::distributeColumnWidths() {
    if (!tableWidget) return;
    autoResizingColumns = true;
    int available = tableWidget->viewport()->width() - tableWidget->columnWidth(9);
    if (available <= 100) { autoResizingColumns = false; return; }

    const double defaultWeights[] = {3.0, 1.0, 1.5, 2.0, 1.0, 1.5, 1.5, 2.0, 1.0};
    double ratios[9];
    bool hasCustom = false;

    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    if (s.contains("columnRatios")) {
        QStringList parts = s.value("columnRatios").toString().split(",");
        if (parts.size() == 9) {
            hasCustom = true;
            double sum = 0;
            for (int i = 0; i < 9; i++) { ratios[i] = parts[i].toDouble(); sum += ratios[i]; }
            if (sum <= 0.001) hasCustom = false;
            else for (int i = 0; i < 9; i++) ratios[i] /= sum;
        }
    }
    if (!hasCustom) {
        double sum = 0;
        for (int i = 0; i < 9; i++) { ratios[i] = defaultWeights[i]; sum += ratios[i]; }
        for (int i = 0; i < 9; i++) ratios[i] /= sum;
    }

    double visibleSum = 0;
    for (int i = 0; i < 9; i++) {
        if (!tableWidget->isColumnHidden(i)) visibleSum += ratios[i];
    }
    if (visibleSum <= 0.001) { autoResizingColumns = false; return; }

    int remaining = available;
    int lastVisible = -1;
    for (int i = 0; i < 9; i++) {
        if (!tableWidget->isColumnHidden(i)) lastVisible = i;
    }
    for (int i = 0; i < 9; i++) {
        if (tableWidget->isColumnHidden(i)) continue;
        if (i == lastVisible) {
            tableWidget->setColumnWidth(i, qMax(40, remaining));
        } else {
            int w = qMax(40, (int)(available * ratios[i] / visibleSum));
            tableWidget->setColumnWidth(i, w);
            remaining -= w;
        }
    }
    autoResizingColumns = false;
}

void SuperGuardian::saveColumnWidths() {
    if (autoResizingColumns) return;
    double total = 0;
    for (int i = 0; i < 9; i++) total += tableWidget->columnWidth(i);
    if (total <= 0) return;
    QStringList parts;
    for (int i = 0; i < 9; i++)
        parts.append(QString::number(tableWidget->columnWidth(i) / total, 'f', 6));
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("columnRatios", parts.join(","));
}

void SuperGuardian::resetColumnWidths() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.remove("columnRatios");
    distributeColumnWidths();
}

// ---- 列显示/隐藏管理 ----

void SuperGuardian::saveColumnVisibility() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QStringList hidden;
    for (int i = 0; i < 9; i++) {
        if (tableWidget->isColumnHidden(i)) hidden << QString::number(i);
    }
    s.setValue("hiddenColumns", hidden.join(","));
}

void SuperGuardian::restoreColumnVisibility() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    if (!s.contains("hiddenColumns")) {
        tableWidget->setColumnHidden(5, true);
        return;
    }
    QString hidden = s.value("hiddenColumns").toString();
    if (hidden.isEmpty()) return;
    for (const QString& col : hidden.split(",")) {
        int i = col.toInt();
        if (i >= 0 && i < 9) tableWidget->setColumnHidden(i, true);
    }
}

void SuperGuardian::onHeaderContextMenu(const QPoint& pos) {
    QHeaderView* header = tableWidget->horizontalHeader();
    int clickedSection = header->logicalIndexAt(pos);
    if (clickedSection == 9) return;

    QMenu menu(this);
    for (int v = 0; v < header->count(); v++) {
        int i = header->logicalIndex(v);
        if (i == 9) continue;
        QTableWidgetItem* hdr = tableWidget->horizontalHeaderItem(i);
        if (!hdr) continue;
        QAction* act = menu.addAction(hdr->text());
        act->setCheckable(true);
        act->setChecked(!tableWidget->isColumnHidden(i));
        connect(act, &QAction::toggled, this, [this, i](bool checked) {
            tableWidget->setColumnHidden(i, !checked);
            saveColumnVisibility();
            distributeColumnWidths();
        });
    }
    menu.exec(header->mapToGlobal(pos));
}

void SuperGuardian::saveHeaderOrder() {
    QHeaderView* header = tableWidget->horizontalHeader();
    QStringList order;
    for (int v = 0; v < header->count(); v++)
        order << QString::number(header->logicalIndex(v));
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("headerOrder", order.join(","));
}

void SuperGuardian::restoreHeaderOrder() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QString orderStr = s.value("headerOrder").toString();
    if (orderStr.isEmpty()) return;
    QStringList parts = orderStr.split(",");
    QHeaderView* header = tableWidget->horizontalHeader();
    int count = header->count();
    if (parts.size() != count) return;
    m_revertingHeader = true;
    for (int v = 0; v < count; v++) {
        int logical = parts[v].toInt();
        if (logical < 0 || logical >= count) continue;
        int currentVisual = header->visualIndex(logical);
        if (currentVisual != v) header->moveSection(currentVisual, v);
    }
    int opVisual = header->visualIndex(9);
    if (opVisual != count - 1) header->moveSection(opVisual, count - 1);
    m_revertingHeader = false;
}

void SuperGuardian::resetHeaderDisplay() {
    QHeaderView* header = tableWidget->horizontalHeader();
    m_revertingHeader = true;
    for (int i = 0; i < header->count(); i++) {
        int curVisual = header->visualIndex(i);
        if (curVisual != i) header->moveSection(curVisual, i);
    }
    for (int i = 0; i < 9; i++)
        tableWidget->setColumnHidden(i, i == 5);
    m_revertingHeader = false;
    saveHeaderOrder();
    saveColumnVisibility();
    distributeColumnWidths();
}
