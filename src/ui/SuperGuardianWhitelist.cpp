#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "AppStorage.h"
#include "ConfigDatabase.h"
#include "GuardTableWidgets.h"
#include "ProcessUtils.h"
#include <QtWidgets>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QRegularExpression>
#include <algorithm>

// ---- 允许重复添加的程序白名单 ----

using namespace Qt::Literals::StringLiterals;

namespace {

QString normalizeWhitelistPath(const QString& rawPath) {
    QString path = rawPath.trimmed();
    if (path.isEmpty())
        return {};

    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.mid(1, path.size() - 2).trimmed();

    QFileInfo fileInfo(path);
    if (fileInfo.exists() && fileInfo.suffix().compare(u"lnk", Qt::CaseInsensitive) == 0) {
        const QString resolved = resolveShortcut(fileInfo.absoluteFilePath());
        if (!resolved.isEmpty() && resolved.compare(path, Qt::CaseInsensitive) != 0) {
            path = resolved;
            fileInfo = QFileInfo(path);
        }
    }

    if (!fileInfo.exists()) {
        QString found = QStandardPaths::findExecutable(path);
        if (found.isEmpty())
            found = QStandardPaths::findExecutable(fileInfo.fileName());
        if (!found.isEmpty()) {
            path = found;
            fileInfo = QFileInfo(path);
        }
    }

    const QString canonicalPath = fileInfo.exists() ? fileInfo.canonicalFilePath() : QString();
    if (!canonicalPath.isEmpty())
        path = canonicalPath;
    else if (fileInfo.isAbsolute())
        path = fileInfo.absoluteFilePath();

    return QDir::toNativeSeparators(path.trimmed());
}

QString whitelistNameForPath(const QString& path) {
    const QString fileName = QFileInfo(path).fileName();
    return fileName.isEmpty() ? path : fileName;
}

QList<int> selectedRowsOf(DesktopSelectTable* table) {
    QList<int> rows;
    if (!table || !table->selectionModel())
        return rows;

    for (const QModelIndex& index : table->selectionModel()->selectedRows())
        rows.append(index.row());
    std::sort(rows.begin(), rows.end());
    return rows;
}

bool containsWhitelistPath(DesktopSelectTable* table, const QString& path, int skipRow = -1) {
    if (!table)
        return false;

    for (int row = 0; row < table->rowCount(); ++row) {
        if (row == skipRow)
            continue;
        if (QTableWidgetItem* item = table->item(row, 1)) {
            if (item->text().compare(path, Qt::CaseInsensitive) == 0)
                return true;
        }
    }
    return false;
}

void updateWhitelistRow(DesktopSelectTable* table, int row, const QString& path) {
    auto ensureItem = [table, row](int column) {
        if (!table->item(row, column))
            table->setItem(row, column, new QTableWidgetItem());
        return table->item(row, column);
    };

    QTableWidgetItem* nameItem = ensureItem(0);
    QTableWidgetItem* pathItem = ensureItem(1);

    nameItem->setFlags((nameItem->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
    pathItem->setFlags(pathItem->flags() | Qt::ItemIsEditable);

    const QString displayName = whitelistNameForPath(path);
    nameItem->setText(displayName);
    nameItem->setToolTip(displayName);
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

QStringList collectWhitelistPaths(DesktopSelectTable* table) {
    QStringList result;
    QSet<QString> seen;
    if (!table)
        return result;

    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* pathItem = table->item(row, 1);
        if (!pathItem)
            continue;

        const QString normalized = normalizeWhitelistPath(pathItem->text());
        if (normalized.isEmpty())
            continue;

        const QString key = normalized.toLower();
        if (seen.contains(key))
            continue;

        seen.insert(key);
        result.append(normalized);
    }
    return result;
}

void refreshWhitelistFilter(DesktopSelectTable* table, QLabel* statsLabel, const QString& keyword) {
    const QString filter = keyword.trimmed();
    int visibleRows = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString name = table->item(row, 0) ? table->item(row, 0)->text() : QString();
        const QString path = table->item(row, 1) ? table->item(row, 1)->text() : QString();
        const bool matched = filter.isEmpty()
            || name.contains(filter, Qt::CaseInsensitive)
            || path.contains(filter, Qt::CaseInsensitive);
        table->setRowHidden(row, !matched);
        if (matched)
            ++visibleRows;
    }

    if (statsLabel) {
        statsLabel->setText(filter.isEmpty()
            ? QString(u"共 %1 条"_s).arg(table->rowCount())
            : QString(u"显示 %1 / %2 条"_s).arg(visibleRows).arg(table->rowCount()));
    }
}

QStringList importWhitelistEntries(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QByteArray raw = file.readAll();
    file.close();

    if (filePath.endsWith(u".json", Qt::CaseInsensitive)) {
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        QJsonArray array;
        if (doc.isArray())
            array = doc.array();
        else if (doc.isObject()) {
            QJsonObject object = doc.object();
            if (object.value(u"paths").isArray())
                array = object.value(u"paths").toArray();
            else if (object.value(u"items").isArray())
                array = object.value(u"items").toArray();
        }

        QStringList result;
        for (const QJsonValue& value : array) {
            if (value.isString())
                result.append(value.toString());
        }
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
        QJsonArray array;
        for (const QString& path : paths)
            array.append(path);
        file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    } else {
        file.write(paths.join(u"\r\n"_s).toUtf8());
    }
    file.close();
    return true;
}

class WhitelistTable final : public DesktopSelectTable {
public:
    using DesktopSelectTable::DesktopSelectTable;
    std::function<void(const QStringList&)> onFilesDropped;
    std::function<void(const QStringList&)> onTextPasted;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls())
            event->acceptProposedAction();
        else
            DesktopSelectTable::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (event->mimeData()->hasUrls())
            event->acceptProposedAction();
        else
            DesktopSelectTable::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override {
        if (!event->mimeData()->hasUrls()) {
            DesktopSelectTable::dropEvent(event);
            return;
        }

        QStringList files;
        for (const QUrl& url : event->mimeData()->urls()) {
            const QString file = url.toLocalFile();
            if (!file.isEmpty())
                files.append(file);
        }
        if (onFilesDropped)
            onFilesDropped(files);
        event->acceptProposedAction();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->matches(QKeySequence::Paste) && onTextPasted) {
            const QString text = QGuiApplication::clipboard()->text().trimmed();
            if (!text.isEmpty()) {
                onTextPasted(text.split(QRegularExpression(u"[\r\n]+"_s), Qt::SkipEmptyParts));
                return;
            }
        }
        DesktopSelectTable::keyPressEvent(event);
    }
};

}

void SuperGuardian::showDuplicateWhitelistDialog() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"允许重复添加的程序"_s);
    dlg.resize(900, 560);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"以下程序允许重复添加到列表中。支持多选、拖放、搜索、导入、导出和直接编辑路径；系统内置工具默认允许重复添加。"_s));

    QHBoxLayout* searchLay = new QHBoxLayout();
    searchLay->addWidget(new QLabel(u"搜索："_s));
    QLineEdit* searchEdit = new QLineEdit();
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setPlaceholderText(u"按程序名或路径搜索，适合大量数据快速定位"_s);
    searchLay->addWidget(searchEdit);
    QLabel* statsLabel = new QLabel();
    searchLay->addWidget(statsLabel);
    lay->addLayout(searchLay);

    auto* tableWidget = new WhitelistTable();
    tableWidget->setColumnCount(2);
    tableWidget->setHorizontalHeaderLabels({ u"程序"_s, u"程序路径"_s });
    tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableWidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    tableWidget->setAlternatingRowColors(true);
    tableWidget->verticalHeader()->setDefaultSectionSize(24);
    tableWidget->verticalHeader()->setVisible(false);
    tableWidget->setMouseTracking(true);
    tableWidget->setItemDelegate(new BruteForceDelegate(tableWidget));
    lay->addWidget(tableWidget);

    bool updatingTable = false;

    auto addPaths = [&](const QStringList& paths) {
        int added = 0;
        updatingTable = true;
        for (const QString& path : paths) {
            if (addWhitelistPath(tableWidget, path))
                ++added;
        }
        updatingTable = false;
        refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
        return added;
    };

    addPaths(duplicateWhitelist);

    tableWidget->onFilesDropped = [&](const QStringList& files) {
        addPaths(files);
    };
    tableWidget->onTextPasted = [&](const QStringList& lines) {
        addPaths(lines);
    };
    tableWidget->onDeletePressed = [&](const QList<int>& rows) {
        for (int i = rows.size() - 1; i >= 0; --i)
            tableWidget->removeRow(rows[i]);
        refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
    };

    QObject::connect(searchEdit, &QLineEdit::textChanged, &dlg, [&](const QString& text) {
        refreshWhitelistFilter(tableWidget, statsLabel, text);
    });

    QObject::connect(tableWidget, &QTableWidget::itemChanged, &dlg, [&](QTableWidgetItem* changedItem) {
        if (updatingTable || !changedItem || changedItem->column() != 1)
            return;

        const int row = changedItem->row();
        const QString normalized = normalizeWhitelistPath(changedItem->text());
        if (normalized.isEmpty()) {
            tableWidget->removeRow(row);
            refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
            return;
        }
        if (containsWhitelistPath(tableWidget, normalized, row)) {
            tableWidget->removeRow(row);
            QToolTip::showText(QCursor::pos(), u"该程序已在白名单中。"_s, tableWidget);
            refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
            return;
        }

        updatingTable = true;
        updateWhitelistRow(tableWidget, row, normalized);
        updatingTable = false;
        refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
    });

    QHBoxLayout* editLay = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton(u"添加程序"_s);
    QPushButton* importBtn = new QPushButton(u"导入"_s);
    QPushButton* exportBtn = new QPushButton(u"导出"_s);
    QPushButton* removeBtn = new QPushButton(u"删除选中"_s);
    QPushButton* selectAllBtn = new QPushButton(u"全选可见项"_s);
    editLay->addWidget(addBtn);
    editLay->addWidget(importBtn);
    editLay->addWidget(exportBtn);
    editLay->addWidget(removeBtn);
    editLay->addWidget(selectAllBtn);
    editLay->addStretch();
    lay->addLayout(editLay);

    QObject::connect(addBtn, &QPushButton::clicked, [&]() {
        QStringList files = QFileDialog::getOpenFileNames(&dlg,
            u"选择程序"_s, QString(), u"Executable (*.exe);;Shortcut (*.lnk);;All Files (*)"_s);
        addPaths(files);
    });
    QObject::connect(importBtn, &QPushButton::clicked, [&]() {
        const QString filePath = QFileDialog::getOpenFileName(&dlg,
            u"导入白名单"_s, QString(), u"支持的文件 (*.txt *.json);;文本文件 (*.txt);;JSON 文件 (*.json)"_s);
        if (filePath.isEmpty())
            return;

        const QStringList importedPaths = importWhitelistEntries(filePath);
        const int added = addPaths(importedPaths);
        showMessageDialog(&dlg, u"导入完成"_s,
            u"共读取 %1 条，成功导入 %2 条。"_s.arg(importedPaths.size()).arg(added));
    });
    QObject::connect(exportBtn, &QPushButton::clicked, [&]() {
        const QString filePath = QFileDialog::getSaveFileName(&dlg,
            u"导出白名单"_s, u"duplicate_whitelist.json"_s,
            u"JSON 文件 (*.json);;文本文件 (*.txt)"_s);
        if (filePath.isEmpty())
            return;

        const QStringList paths = collectWhitelistPaths(tableWidget);
        if (!exportWhitelistEntries(filePath, paths)) {
            showMessageDialog(&dlg, u"导出失败"_s, u"无法写入导出文件。"_s);
            return;
        }
        showMessageDialog(&dlg, u"导出完成"_s, u"白名单已成功导出。"_s);
    });
    QObject::connect(removeBtn, &QPushButton::clicked, [&]() {
        const QList<int> rows = selectedRowsOf(tableWidget);
        for (int i = rows.size() - 1; i >= 0; --i)
            tableWidget->removeRow(rows[i]);
        refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
    });
    QObject::connect(selectAllBtn, &QPushButton::clicked, [&]() {
        tableWidget->clearSelection();
        for (int row = 0; row < tableWidget->rowCount(); ++row) {
            if (tableWidget->isRowHidden(row))
                continue;
            tableWidget->selectionModel()->select(tableWidget->model()->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    });

    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn); btnLay->addWidget(cancelBtn); btnLay->addStretch();
    lay->addLayout(btnLay);

    if (dlg.exec() != QDialog::Accepted) return;
    duplicateWhitelist = collectWhitelistPaths(tableWidget);
    ConfigDatabase::instance().setValue(u"duplicateWhitelist"_s, duplicateWhitelist.join(u"|"_s));
}

// ---- 测试程序是否允许重复添加 ----

void SuperGuardian::testDuplicateAdd() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"测试程序是否允许重复添加"_s);
    dlg.setFixedWidth(500);
    dlg.setMinimumHeight(160);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"输入程序完整路径或名称，或拖放文件到输入框："_s));

    class DropLineEdit : public QLineEdit {
    public:
        DropLineEdit(QWidget* p = nullptr) : QLineEdit(p) { setAcceptDrops(true); }
    protected:
        void dragEnterEvent(QDragEnterEvent* e) override { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
        void dropEvent(QDropEvent* e) override {
            const QList<QUrl> urls = e->mimeData()->urls();
            if (!urls.isEmpty()) setText(urls.first().toLocalFile());
        }
    };

    DropLineEdit* input = new DropLineEdit();
    input->setPlaceholderText(u"程序路径或名称，支持拖放和快捷方式"_s);
    lay->addWidget(input);

    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"检测"_s);
    QPushButton* closeBtn = new QPushButton(u"关闭"_s);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(closeBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);

    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, [&]() {
        QString path = input->text().trimmed();
        if (path.isEmpty()) {
            showMessageDialog(&dlg, u"提示"_s, u"请输入程序路径或名称。"_s);
            return;
        }
        QString resolvedPath = path;
        QFileInfo fi(path);
        bool isSystemTool = false;
        if (fi.exists() && fi.suffix().toLower() == "lnk") {
            QString lnkTarget = resolveShortcut(path);
            if (lnkTarget.isEmpty() || lnkTarget.compare(path, Qt::CaseInsensitive) == 0) {
                showMessageDialog(&dlg, u"检测结果"_s,
                    u"无法解析快捷方式的目标程序。"_s);
                return;
            }
            resolvedPath = lnkTarget;
            fi = QFileInfo(resolvedPath);
        }
        if (!fi.exists()) {
            QString found = QStandardPaths::findExecutable(resolvedPath);
            if (found.isEmpty()) {
                showMessageDialog(&dlg, u"检测结果"_s,
                    u"程序不存在，也不是系统内置工具，无法添加。"_s);
                return;
            }
            resolvedPath = found;
            isSystemTool = true;
        }
        if (!isSystemTool) {
            QString nameOnly = QFileInfo(resolvedPath).fileName();
            QString found = QStandardPaths::findExecutable(nameOnly);
            if (!found.isEmpty() && QFileInfo(found).canonicalFilePath() == QFileInfo(resolvedPath).canonicalFilePath())
                isSystemTool = true;
        }
        QString targetPath = resolveShortcut(resolvedPath);
        QString displayName = QFileInfo(targetPath).fileName();
        if (isSystemTool) {
            showMessageDialog(&dlg, u"检测结果"_s,
                u"「%1」是系统内置工具，允许重复添加。"_s.arg(displayName));
            return;
        }
        if (duplicateWhitelist.contains(targetPath, Qt::CaseInsensitive) ||
            duplicateWhitelist.contains(resolvedPath, Qt::CaseInsensitive)) {
            showMessageDialog(&dlg, u"检测结果"_s,
                u"「%1」在重复添加白名单中，允许重复添加。"_s.arg(displayName));
            return;
        }
        showMessageDialog(&dlg, u"检测结果"_s,
            u"「%1」不是系统内置工具，也不在白名单中，不允许重复添加。"_s.arg(displayName));
    });

    dlg.exec();
}
