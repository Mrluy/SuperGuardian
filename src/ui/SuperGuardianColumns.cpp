#include "SuperGuardian.h"
#include "AppStorage.h"
#include "ConfigDatabase.h"
#include <QtWidgets>
#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr int kActionColumn = 10;
constexpr int kDataColumnCount = 10;
constexpr int kDefaultHiddenColumns[] = { 0, 6 };

}

// ---- Column width management ----

void SuperGuardian::distributeColumnWidths() {
    if (!tableWidget) return;
    autoResizingColumns = true;
    int available = tableWidget->viewport()->width() - tableWidget->columnWidth(kActionColumn);
    if (available <= 100) { autoResizingColumns = false; return; }

    const double defaultWeights[kDataColumnCount] = {1.0, 3.0, 1.0, 1.5, 2.0, 1.0, 1.5, 1.5, 2.0, 1.0};
    double ratios[kDataColumnCount];
    bool hasCustom = false;

    auto& cfg = ConfigDatabase::instance();
    if (cfg.contains(u"columnRatios"_s)) {
        QStringList parts = cfg.value(u"columnRatios"_s).toString().split(u","_s);
        if (parts.size() == kDataColumnCount) {
            hasCustom = true;
            double sum = 0;
            for (int i = 0; i < kDataColumnCount; i++) { ratios[i] = parts[i].toDouble(); sum += ratios[i]; }
            if (sum <= 0.001) hasCustom = false;
            else for (int i = 0; i < kDataColumnCount; i++) ratios[i] /= sum;
        }
    }
    if (!hasCustom) {
        double sum = 0;
        for (int i = 0; i < kDataColumnCount; i++) { ratios[i] = defaultWeights[i]; sum += ratios[i]; }
        for (int i = 0; i < kDataColumnCount; i++) ratios[i] /= sum;
    }

    double visibleSum = 0;
    for (int i = 0; i < kDataColumnCount; i++) {
        if (!tableWidget->isColumnHidden(i)) visibleSum += ratios[i];
    }
    if (visibleSum <= 0.001) { autoResizingColumns = false; return; }

    int remaining = available;
    int lastVisible = -1;
    for (int i = 0; i < kDataColumnCount; i++) {
        if (!tableWidget->isColumnHidden(i)) lastVisible = i;
    }
    for (int i = 0; i < kDataColumnCount; i++) {
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
    for (int i = 0; i < kDataColumnCount; i++) total += tableWidget->columnWidth(i);
    if (total <= 0) return;
    QStringList parts;
    for (int i = 0; i < kDataColumnCount; i++)
        parts.append(QString::number(tableWidget->columnWidth(i) / total, 'f', 6));
    ConfigDatabase::instance().setValue(u"columnRatios"_s, parts.join(u","_s));
}

void SuperGuardian::resetColumnWidths() {
    ConfigDatabase::instance().remove(u"columnRatios"_s);
    distributeColumnWidths();
}

// ---- 列显示/隐藏管理 ----

void SuperGuardian::saveColumnVisibility() {
    QStringList hidden;
    for (int i = 0; i < kDataColumnCount; i++) {
        if (tableWidget->isColumnHidden(i)) hidden << QString::number(i);
    }
    ConfigDatabase::instance().setValue(u"hiddenColumns"_s, hidden.join(u","_s));
}

void SuperGuardian::restoreColumnVisibility() {
    for (int i = 0; i < kDataColumnCount; ++i)
        tableWidget->setColumnHidden(i, false);

    auto& cfg = ConfigDatabase::instance();
    if (!cfg.contains(u"hiddenColumns"_s)) {
        for (int col : kDefaultHiddenColumns)
            tableWidget->setColumnHidden(col, true);
        return;
    }

    QString hidden = cfg.value(u"hiddenColumns"_s).toString();
    if (hidden.isEmpty()) return;
    for (const QString& col : hidden.split(u","_s)) {
        bool ok = false;
        int i = col.toInt(&ok);
        if (ok && i >= 0 && i < kDataColumnCount)
            tableWidget->setColumnHidden(i, true);
    }
}

void SuperGuardian::onHeaderContextMenu(const QPoint& pos) {
    QHeaderView* header = tableWidget->horizontalHeader();
    int clickedSection = header->logicalIndexAt(pos);
    if (clickedSection == kActionColumn) return;

    QMenu menu(this);
    for (int v = 0; v < header->count(); v++) {
        int i = header->logicalIndex(v);
        if (i == kActionColumn) continue;
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
    ConfigDatabase::instance().setValue(u"headerOrder"_s, order.join(u","_s));
}

void SuperGuardian::restoreHeaderOrder() {
    QString orderStr = ConfigDatabase::instance().value(u"headerOrder"_s).toString();
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
    int opVisual = header->visualIndex(kActionColumn);
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
    for (int i = 0; i < kDataColumnCount; i++) {
        bool hide = false;
        for (int col : kDefaultHiddenColumns) { if (i == col) { hide = true; break; } }
        tableWidget->setColumnHidden(i, hide);
    }
    m_revertingHeader = false;
    saveHeaderOrder();
    saveColumnVisibility();
    distributeColumnWidths();
}

void SuperGuardian::performSort() {
    if (sortState == 0 || activeSortSection < 0 || activeSortSection >= kDataColumnCount) {
        std::sort(items.begin(), items.end(), [](const GuardItem& a, const GuardItem& b) {
            if (a.pinned != b.pinned)
                return a.pinned > b.pinned;
            return a.insertionOrder < b.insertionOrder;
        });
        rebuildTableFromItems();
        tableWidget->horizontalHeader()->setSortIndicatorShown(false);
        return;
    }

    const Qt::SortOrder order = (sortState == 1) ? Qt::AscendingOrder : Qt::DescendingOrder;
    if (tableWidget->rowCount() == 0 && !items.isEmpty())
        rebuildTableFromItems();

    auto collectRows = [this](bool pinned) {
        QVector<QPair<QString, int>> rows;
        rows.reserve(items.size());
        for (int row = 0; row < tableWidget->rowCount(); ++row) {
            int idx = findItemIndexById(rowId(row));
            if (idx < 0 || items[idx].pinned != pinned)
                continue;

            QTableWidgetItem* cell = tableWidget->item(row, activeSortSection);
            rows.append({ cell ? cell->text() : QString(), idx });
        }
        return rows;
    };
    auto sortRows = [order](QVector<QPair<QString, int>>& rows) {
        std::sort(rows.begin(), rows.end(), [order](const auto& a, const auto& b) {
            const int compareResult = a.first.localeAwareCompare(b.first);
            return order == Qt::AscendingOrder ? compareResult < 0 : compareResult > 0;
        });
    };

    auto pinnedRows = collectRows(true);
    auto unpinnedRows = collectRows(false);
    sortRows(pinnedRows);
    sortRows(unpinnedRows);

    QVector<GuardItem> sortedItems;
    sortedItems.reserve(items.size());
    for (const auto& row : pinnedRows)
        sortedItems.append(items[row.second]);
    for (const auto& row : unpinnedRows)
        sortedItems.append(items[row.second]);

    items = sortedItems;
    rebuildTableFromItems();

    QHeaderView* header = tableWidget->horizontalHeader();
    header->setSortIndicatorShown(true);
    header->setSortIndicator(activeSortSection, order);
}

void SuperGuardian::saveSortState() {
    auto& db = ConfigDatabase::instance();
    db.setValue(u"sortSection"_s, activeSortSection);
    db.setValue(u"sortState"_s, sortState);
}
