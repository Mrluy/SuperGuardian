#include "WhitelistData.h"
#include "DialogHelpers.h"
#include "GuardTableWidgets.h"
#include "ProcessUtils.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalBlocker>

using namespace Qt::Literals::StringLiterals;

// ---- 路径处理 ----

QString normalizeWhitelistPath(const QString& rawPath) {
    QString path = rawPath.trimmed();
    if (path.isEmpty())
        return {};

    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.mid(1, path.size() - 2).trimmed();

    QFileInfo fi(path);
    if (fi.exists() && fi.suffix().compare(u"lnk", Qt::CaseInsensitive) == 0) {
        const QString resolved = resolveShortcut(fi.absoluteFilePath());
        if (!resolved.isEmpty() && resolved.compare(path, Qt::CaseInsensitive) != 0) {
            path = resolved;
            fi = QFileInfo(path);
        }
    }

    if (!fi.exists()) {
        QString found = QStandardPaths::findExecutable(path);
        if (found.isEmpty())
            found = QStandardPaths::findExecutable(fi.fileName());
        if (!found.isEmpty()) {
            path = found;
            fi = QFileInfo(path);
        }
    }

    const QString canonical = fi.exists() ? fi.canonicalFilePath() : QString();
    if (!canonical.isEmpty())
        path = canonical;
    else if (fi.isAbsolute())
        path = fi.absoluteFilePath();

    return QDir::toNativeSeparators(path.trimmed());
}

QString whitelistNameForPath(const QString& path) {
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

// ---- 表格行操作 ----

QList<int> selectedWhitelistRows(DesktopSelectTable* table) {
    QList<int> rows;
    if (!table || !table->selectionModel())
        return rows;
    for (const QModelIndex& idx : table->selectionModel()->selectedRows())
        rows.append(idx.row());
    std::sort(rows.begin(), rows.end());
    return rows;
}

bool containsWhitelistPath(DesktopSelectTable* table, const QString& path, int skipRow) {
    if (!table)
        return false;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (row == skipRow)
            continue;
        if (auto* item = table->item(row, 1))
            if (item->text().compare(path, Qt::CaseInsensitive) == 0)
                return true;
    }
    return false;
}

void updateWhitelistRow(DesktopSelectTable* table, int row, const QString& path) {
    auto ensureItem = [table, row](int col) {
        if (!table->item(row, col))
            table->setItem(row, col, new QTableWidgetItem());
        return table->item(row, col);
    };
    auto* nameItem = ensureItem(0);
    auto* pathItem = ensureItem(1);
    nameItem->setFlags((nameItem->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
    pathItem->setFlags(pathItem->flags() | Qt::ItemIsEditable);
    const QString display = whitelistNameForPath(path);
    nameItem->setText(display);
    nameItem->setToolTip(display);
    nameItem->setIcon(getFileIcon(path));
    pathItem->setText(path);
    pathItem->setToolTip(path);
}

bool addWhitelistPath(DesktopSelectTable* table, const QString& rawPath) {
    const QString path = normalizeWhitelistPath(rawPath);
    if (path.isEmpty() || containsWhitelistPath(table, path))
        return false;
    const int row = table->rowCount();
    table->insertRow(row);
    updateWhitelistRow(table, row, path);
    return true;
}

// ---- 批量操作 ----

static QStringList collectPathsImpl(DesktopSelectTable* table, int startRow, int endRow) {
    QStringList result;
    QSet<QString> seen;
    for (int row = startRow; row < endRow; ++row) {
        auto* item = table->item(row, 1);
        if (!item)
            continue;
        const QString norm = normalizeWhitelistPath(item->text());
        if (norm.isEmpty())
            continue;
        const QString key = norm.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        result.append(norm);
    }
    return result;
}

QStringList collectWhitelistPaths(DesktopSelectTable* table) {
    if (!table)
        return {};
    return collectPathsImpl(table, 0, table->rowCount());
}

QStringList collectWhitelistPaths(DesktopSelectTable* table, const QList<int>& rows) {
    QStringList result;
    QSet<QString> seen;
    for (int row : rows) {
        if (!table || row < 0 || row >= table->rowCount())
            continue;
        auto* item = table->item(row, 1);
        if (!item)
            continue;
        const QString norm = normalizeWhitelistPath(item->text());
        if (norm.isEmpty())
            continue;
        const QString key = norm.toLower();
        if (!seen.contains(key)) {
            seen.insert(key);
            result.append(norm);
        }
    }
    return result;
}

int addWhitelistPaths(DesktopSelectTable* table, QLabel* statsLabel, QLineEdit* searchEdit,
    const QStringList& paths, bool replaceExisting, bool* updatingTable) {
    if (!table || !statsLabel || !searchEdit || !updatingTable)
        return 0;
    int added = 0;
    *updatingTable = true;
    const QSignalBlocker blocker(table);
    if (replaceExisting) {
        table->clearContents();
        table->setRowCount(0);
    }
    for (const QString& path : paths) {
        if (addWhitelistPath(table, path))
            ++added;
    }
    *updatingTable = false;
    refreshWhitelistFilter(table, statsLabel, searchEdit->text());
    return added;
}

// ---- 搜索过滤 ----

void refreshWhitelistFilter(DesktopSelectTable* table, QLabel* statsLabel, const QString& keyword) {
    const QString filter = keyword.trimmed();
    int visible = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString name = table->item(row, 0) ? table->item(row, 0)->text() : QString();
        const QString path = table->item(row, 1) ? table->item(row, 1)->text() : QString();
        const bool match = filter.isEmpty()
            || name.contains(filter, Qt::CaseInsensitive)
            || path.contains(filter, Qt::CaseInsensitive);
        table->setRowHidden(row, !match);
        if (match)
            ++visible;
    }
    if (statsLabel) {
        statsLabel->setText(filter.isEmpty()
            ? u"共 %1 条"_s.arg(table->rowCount())
            : u"显示 %1 / %2 条"_s.arg(visible).arg(table->rowCount()));
    }
}

// ---- 导入导出 ----

QStringList importWhitelistEntries(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QByteArray raw = file.readAll();
    file.close();

    if (filePath.endsWith(u".json", Qt::CaseInsensitive)) {
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        QJsonArray arr;
        if (doc.isArray())
            arr = doc.array();
        else if (doc.isObject()) {
            auto obj = doc.object();
            if (obj.value(u"paths").isArray())
                arr = obj.value(u"paths").toArray();
            else if (obj.value(u"items").isArray())
                arr = obj.value(u"items").toArray();
        }
        QStringList result;
        for (const QJsonValue& v : arr)
            if (v.isString())
                result.append(v.toString());
        return result;
    }

    QStringList result;
    for (const QString& line : QString::fromUtf8(raw).split(QRegularExpression(u"[\r\n]+"_s), Qt::SkipEmptyParts))
        result.append(line.trimmed());
    return result;
}

bool exportWhitelistEntries(const QString& filePath, const QStringList& paths) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    if (filePath.endsWith(u".json", Qt::CaseInsensitive)) {
        QJsonArray arr;
        for (const QString& p : paths)
            arr.append(p);
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    } else {
        file.write(paths.join(u"\r\n"_s).toUtf8());
    }
    file.close();
    return true;
}

// ---- UI 辅助 ----

int promptWhitelistImportMode(QWidget* parent, const QString& title) {
    bool ok = false;
    const QString mode = showItemDialog(parent, title, u"请选择导入方式："_s,
        { u"增量导入（保留现有项）"_s, u"覆盖导入（清空后导入）"_s }, &ok);
    if (!ok)
        return 0;
    return mode.startsWith(u"覆盖"_s) ? 2 : 1;
}

QString buildDropConfirmText(const QStringList& files) {
    if (files.isEmpty())
        return u"未检测到可添加的文件。"_s;
    QStringList preview;
    const int n = qMin(5, files.size());
    for (int i = 0; i < n; ++i)
        preview.append(u"- %1"_s.arg(QFileInfo(files[i]).fileName().isEmpty() ? files[i] : QFileInfo(files[i]).fileName()));
    if (files.size() > n)
        preview.append(u"- ... 其余 %1 项"_s.arg(files.size() - n));
    return u"确认添加拖入的 %1 个程序吗？\n\n%2"_s.arg(files.size()).arg(preview.join(u"\n"_s));
}
