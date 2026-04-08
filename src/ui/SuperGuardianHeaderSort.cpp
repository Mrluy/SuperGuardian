#include "SuperGuardian.h"
#include "ConfigDatabase.h"
#include <QtWidgets>

using namespace Qt::Literals::StringLiterals;

namespace {
constexpr int kActionColumn = 10;
constexpr int kDataColumnCount = 10;
constexpr int kDefaultHiddenColumns[] = { 0, 6 };
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
            resetColumnWidths();
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
