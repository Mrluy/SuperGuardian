#include "SuperGuardian.h"
#include "AppStorage.h"
#include "ConfigDatabase.h"
#include <QtWidgets>
#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr int kActionColumn = 10;
constexpr int kDataColumnCount = 10;
constexpr int kProgramColumn = 1;
constexpr int kDefaultHiddenColumns[] = { 0, 6 };

int measureHeaderWidth(QTableWidget* tableWidget, int column) {
    if (!tableWidget) return 40;
    QHeaderView* header = tableWidget->horizontalHeader();
    QFontMetrics fm(header->font());
    QTableWidgetItem* hdr = tableWidget->horizontalHeaderItem(column);
    const int textW = hdr ? fm.horizontalAdvance(hdr->text()) : 0;
    return qMax(40, textW + 28);
}

int measureContentWidth(QTableWidget* tableWidget, int column) {
    if (!tableWidget) return 40;

    int required = measureHeaderWidth(tableWidget, column);
    for (int row = 0; row < tableWidget->rowCount(); ++row) {
        if (QWidget* cellWidget = tableWidget->cellWidget(row, column)) {
            required = qMax(required, cellWidget->sizeHint().width() + 12);
        }

        QTableWidgetItem* item = tableWidget->item(row, column);
        if (!item) continue;

        QFontMetrics fm(item->font().family().isEmpty() ? tableWidget->font() : item->font());
        int textW = fm.horizontalAdvance(item->text()) + 16;

        if (!qvariant_cast<QIcon>(item->data(Qt::DecorationRole)).isNull()) {
            // Keep width estimation aligned with delegate painting, which reserves
            // horizontal space for the leading decoration icon before the text.
            textW += 24;
        }

        if (column == kProgramColumn) {
            if (item->data(Qt::UserRole + 2).toBool())
                textW += 20;
        }

        required = qMax(required, textW);
    }

    return required;
}

}

// ---- Column width management ----

void SuperGuardian::distributeColumnWidths() {
    if (!tableWidget) return;
    autoResizingColumns = true;
    int available = tableWidget->viewport()->width() - tableWidget->columnWidth(kActionColumn);
    if (available <= 100) { autoResizingColumns = false; return; }

    // 计算每列的最小宽度（保证表头文字完整显示）
    QHeaderView* header = tableWidget->horizontalHeader();
    QFontMetrics fm(header->font());
    int minWidths[kDataColumnCount];
    for (int i = 0; i < kDataColumnCount; i++) {
        QTableWidgetItem* hdr = tableWidget->horizontalHeaderItem(i);
        int textW = hdr ? fm.horizontalAdvance(hdr->text()) : 0;
        minWidths[i] = textW + 24; // 额外留出排序图标和边距空间
    }

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
    int visibleMinTotal = 0;
    for (int i = 0; i < kDataColumnCount; i++) {
        if (!tableWidget->isColumnHidden(i)) {
            visibleSum += ratios[i];
            visibleMinTotal += minWidths[i];
        }
    }
    if (visibleSum <= 0.001) { autoResizingColumns = false; return; }

    int extraAvailable = qMax(0, available - visibleMinTotal);
    int remaining = available;
    int lastVisible = -1;
    for (int i = 0; i < kDataColumnCount; i++) {
        if (!tableWidget->isColumnHidden(i)) lastVisible = i;
    }
    for (int i = 0; i < kDataColumnCount; i++) {
        if (tableWidget->isColumnHidden(i)) continue;
        if (i == lastVisible) {
            tableWidget->setColumnWidth(i, qMax(0, remaining));
        } else {
            int extra = (int)(extraAvailable * ratios[i] / visibleSum);
            int w = minWidths[i] + extra;
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
    if (!tableWidget) return;

    autoResizingColumns = true;

    const int actionWidth = tableWidget->columnWidth(kActionColumn);
    const int available = tableWidget->viewport()->width() - actionWidth;
    if (available <= 100) {
        autoResizingColumns = false;
        return;
    }

    QVector<int> visibleColumns;
    visibleColumns.reserve(kDataColumnCount);
    for (int i = 0; i < kDataColumnCount; ++i) {
        if (!tableWidget->isColumnHidden(i))
            visibleColumns.append(i);
    }
    if (visibleColumns.isEmpty()) {
        autoResizingColumns = false;
        return;
    }

    QVector<int> headerWidths(kDataColumnCount, 40);
    QVector<int> requiredWidths(kDataColumnCount, 40);
    for (int col : visibleColumns) {
        headerWidths[col] = measureHeaderWidth(tableWidget, col);
        requiredWidths[col] = measureContentWidth(tableWidget, col);
    }

    const bool programVisible = !tableWidget->isColumnHidden(kProgramColumn);
    int otherRequiredTotal = 0;
    for (int col : visibleColumns) {
        if (col == kProgramColumn) continue;
        otherRequiredTotal += requiredWidths[col];
    }

    QVector<int> finalWidths(kDataColumnCount, 0);
    for (int col : visibleColumns) {
        if (col != kProgramColumn)
            finalWidths[col] = requiredWidths[col];
    }

    if (programVisible) {
        int remainingForProgram = available - otherRequiredTotal;
        finalWidths[kProgramColumn] = qMax(remainingForProgram, headerWidths[kProgramColumn]);

        int total = 0;
        for (int col : visibleColumns)
            total += finalWidths[col];

        if (total > available) {
            int overflow = total - available;

            // 先从“程序”列借用
            int programShrinkable = qMax(0, finalWidths[kProgramColumn] - headerWidths[kProgramColumn]);
            int borrowFromProgram = qMin(programShrinkable, overflow);
            finalWidths[kProgramColumn] -= borrowFromProgram;
            overflow -= borrowFromProgram;

            // 若仍然超出，只能把其它列压缩到表头最小宽度，仍保证总宽度不超出可用区域
            if (overflow > 0) {
                for (int col : visibleColumns) {
                    if (col == kProgramColumn || overflow <= 0) continue;
                    int shrinkable = qMax(0, finalWidths[col] - headerWidths[col]);
                    int take = qMin(shrinkable, overflow);
                    finalWidths[col] -= take;
                    overflow -= take;
                }
            }

            // 极端窄窗口兜底：程序列至少保留一个表头宽度，否则取剩余值
            if (overflow > 0) {
                int sumOthers = 0;
                for (int col : visibleColumns) {
                    if (col != kProgramColumn)
                        sumOthers += finalWidths[col];
                }
                finalWidths[kProgramColumn] = qMax(0, available - sumOthers);
            }
        }

        // 若有多余空间，全部给程序列
        int used = 0;
        for (int col : visibleColumns)
            used += finalWidths[col];
        if (used < available)
            finalWidths[kProgramColumn] += available - used;
    } else {
        int used = 0;
        for (int col : visibleColumns) {
            finalWidths[col] = requiredWidths[col];
            used += finalWidths[col];
        }

        if (used > available) {
            int overflow = used - available;
            for (int col : visibleColumns) {
                if (overflow <= 0) break;
                int shrinkable = qMax(0, finalWidths[col] - headerWidths[col]);
                int take = qMin(shrinkable, overflow);
                finalWidths[col] -= take;
                overflow -= take;
            }
        }
    }

    for (int col : visibleColumns)
        tableWidget->setColumnWidth(col, finalWidths[col]);

    autoResizingColumns = false;
}

void SuperGuardian::requestResetColumnWidths() {
    if (pendingResetColumnWidths)
        return;
    pendingResetColumnWidths = true;
    QTimer::singleShot(0, this, [this]() {
        pendingResetColumnWidths = false;
        resetColumnWidths();
    });
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
