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
#include <QPointer>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <algorithm>
#include <memory>

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

QStringList collectWhitelistPaths(DesktopSelectTable* table, const QList<int>& rows) {
    QStringList result;
    QSet<QString> seen;
    for (int row : rows) {
        if (!table || row < 0 || row >= table->rowCount())
            continue;

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

int promptImportMode(QWidget* parent, const QString& title) {
    bool ok = false;
    const QString mode = showItemDialog(parent, title, u"请选择导入方式："_s,
        { u"增量导入（保留现有项）"_s, u"覆盖导入（清空后导入）"_s }, &ok);
    if (!ok)
        return 0;
    return mode.startsWith(u"覆盖"_s) ? 2 : 1;
}

void refreshWhitelistFilter(DesktopSelectTable* table, QLabel* statsLabel, const QString& keyword);

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

QString buildDropConfirmText(const QStringList& files) {
    if (files.isEmpty())
        return u"未检测到可添加的文件。"_s;

    QStringList preview;
    const int previewCount = qMin(5, files.size());
    for (int i = 0; i < previewCount; ++i) {
        const QString file = files[i];
        preview.append(u"- %1"_s.arg(QFileInfo(file).fileName().isEmpty() ? file : QFileInfo(file).fileName()));
    }
    if (files.size() > previewCount)
        preview.append(u"- ... 其余 %1 项"_s.arg(files.size() - previewCount));

    return u"确认添加拖入的 %1 个程序吗？\n\n%2"_s.arg(files.size()).arg(preview.join(u"\n"_s));
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
    static QPointer<QDialog> s_dialog;
    if (s_dialog) {
        s_dialog->showNormal();
        s_dialog->raise();
        s_dialog->activateWindow();
        return;
    }

    auto* dlg = new QDialog(nullptr, kDialogFlags | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    s_dialog = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowModality(Qt::NonModal);
    dlg->setWindowTitle(u"允许重复添加的程序"_s);
    dlg->setWindowIcon(windowIcon());
    dlg->resize(900, 560);

    auto* lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(u"以下程序允许重复添加到列表中。支持多选、Ctrl/Shift 选择、拖放、搜索、导入、导出、导出选中和直接编辑路径；系统内置工具默认允许重复添加。"_s));

    auto* searchLay = new QHBoxLayout();
    searchLay->addWidget(new QLabel(u"搜索："_s));
    auto* searchEdit = new QLineEdit(dlg);
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setPlaceholderText(u"按程序名或路径搜索，适合大量数据快速定位"_s);
    searchLay->addWidget(searchEdit);
    auto* statsLabel = new QLabel(dlg);
    searchLay->addWidget(statsLabel);
    lay->addLayout(searchLay);

    auto* tableWidget = new WhitelistTable(dlg);
    tableWidget->setColumnCount(2);
    tableWidget->setHorizontalHeaderLabels({ u"程序"_s, u"程序路径"_s });
    tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tableWidget->horizontalHeader()->setSectionsMovable(false);
    tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableWidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    tableWidget->setAlternatingRowColors(true);
    tableWidget->verticalHeader()->setDefaultSectionSize(24);
    tableWidget->verticalHeader()->setVisible(false);
    tableWidget->setMouseTracking(true);
    tableWidget->setRowDragEnabled(false);
    tableWidget->setAcceptDrops(true);
    tableWidget->viewport()->setAcceptDrops(true);
    tableWidget->setItemDelegate(new BruteForceDelegate(tableWidget));
    tableWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lay->addWidget(tableWidget, 1);

    auto updatingTable = std::make_shared<bool>(false);

    addWhitelistPaths(tableWidget, statsLabel, searchEdit, duplicateWhitelist, false, updatingTable.get());

    tableWidget->onFilesDropped = [dlg, tableWidget, statsLabel, searchEdit, updatingTable](const QStringList& files) {
        if (files.isEmpty())
            return;
        if (!showMessageDialog(dlg, u"确认添加程序"_s, buildDropConfirmText(files), true))
            return;
        addWhitelistPaths(tableWidget, statsLabel, searchEdit, files, false, updatingTable.get());
    };
    tableWidget->onTextPasted = [tableWidget, statsLabel, searchEdit, updatingTable](const QStringList& lines) {
        addWhitelistPaths(tableWidget, statsLabel, searchEdit, lines, false, updatingTable.get());
    };
    tableWidget->onDeletePressed = [tableWidget, statsLabel, searchEdit](const QList<int>& rows) {
        for (int i = rows.size() - 1; i >= 0; --i)
            tableWidget->removeRow(rows[i]);
        refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
    };

    QObject::connect(searchEdit, &QLineEdit::textChanged, dlg, [tableWidget, statsLabel](const QString& text) {
        refreshWhitelistFilter(tableWidget, statsLabel, text);
    });

    QObject::connect(tableWidget, &QTableWidget::itemChanged, dlg, [tableWidget, statsLabel, searchEdit, updatingTable](QTableWidgetItem* changedItem) {
        if (*updatingTable || !changedItem || changedItem->column() != 1)
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

        *updatingTable = true;
        updateWhitelistRow(tableWidget, row, normalized);
        *updatingTable = false;
        refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
    });

    auto* editLay = new QHBoxLayout();
    auto* addBtn = new QPushButton(u"添加程序"_s, dlg);
    auto* importBtn = new QPushButton(u"导入"_s, dlg);
    auto* exportSelectedBtn = new QPushButton(u"导出选中"_s, dlg);
    auto* exportBtn = new QPushButton(u"导出全部"_s, dlg);
    auto* removeBtn = new QPushButton(u"删除选中"_s, dlg);
    auto* removeAllBtn = new QPushButton(u"删除全部"_s, dlg);
    editLay->addWidget(addBtn);
    editLay->addWidget(importBtn);
    editLay->addWidget(exportSelectedBtn);
    editLay->addWidget(exportBtn);
    editLay->addWidget(removeBtn);
    editLay->addWidget(removeAllBtn);
    editLay->addStretch();
    lay->addLayout(editLay);

    QObject::connect(addBtn, &QPushButton::clicked, dlg, [dlg, tableWidget, statsLabel, searchEdit, updatingTable]() {
        QStringList files = QFileDialog::getOpenFileNames(dlg,
            u"选择程序"_s, QString(), u"Executable (*.exe);;Shortcut (*.lnk);;All Files (*)"_s);
        addWhitelistPaths(tableWidget, statsLabel, searchEdit, files, false, updatingTable.get());
    });
    QObject::connect(importBtn, &QPushButton::clicked, dlg, [dlg, tableWidget, statsLabel, searchEdit, updatingTable]() {
        const QString filePath = QFileDialog::getOpenFileName(dlg,
            u"导入白名单"_s, QString(), u"支持的文件 (*.txt *.json);;文本文件 (*.txt);;JSON 文件 (*.json)"_s);
        if (filePath.isEmpty())
            return;

        const int importMode = promptImportMode(dlg, u"导入白名单"_s);
        if (importMode == 0)
            return;

        const QStringList importedPaths = importWhitelistEntries(filePath);
        if (importedPaths.isEmpty()) {
            showMessageDialog(dlg, u"导入失败"_s, u"导入文件中没有可用的程序路径。"_s);
            return;
        }
        const int added = addWhitelistPaths(tableWidget, statsLabel, searchEdit, importedPaths, importMode == 2, updatingTable.get());
        showMessageDialog(dlg, u"导入完成"_s,
            u"共读取 %1 条，成功导入 %2 条。"_s.arg(importedPaths.size()).arg(added));
    });
    QObject::connect(exportSelectedBtn, &QPushButton::clicked, dlg, [dlg, tableWidget]() {
        const QList<int> rows = selectedRowsOf(tableWidget);
        const QStringList paths = collectWhitelistPaths(tableWidget, rows);
        if (paths.isEmpty()) {
            showMessageDialog(dlg, u"导出选中"_s, u"请先选择至少一项。"_s);
            return;
        }

        const QString filePath = QFileDialog::getSaveFileName(dlg,
            u"导出选中白名单"_s, u"duplicate_whitelist_selected.json"_s,
            u"JSON 文件 (*.json);;文本文件 (*.txt)"_s);
        if (filePath.isEmpty())
            return;

        if (!exportWhitelistEntries(filePath, paths)) {
            showMessageDialog(dlg, u"导出失败"_s, u"无法写入导出文件。"_s);
            return;
        }
        showMessageDialog(dlg, u"导出完成"_s, u"选中白名单已成功导出。"_s);
    });
    QObject::connect(exportBtn, &QPushButton::clicked, dlg, [dlg, tableWidget]() {
        const QString filePath = QFileDialog::getSaveFileName(dlg,
            u"导出白名单"_s, u"duplicate_whitelist.json"_s,
            u"JSON 文件 (*.json);;文本文件 (*.txt)"_s);
        if (filePath.isEmpty())
            return;

        const QStringList paths = collectWhitelistPaths(tableWidget);
        if (!exportWhitelistEntries(filePath, paths)) {
            showMessageDialog(dlg, u"导出失败"_s, u"无法写入导出文件。"_s);
            return;
        }
        showMessageDialog(dlg, u"导出完成"_s, u"白名单已成功导出。"_s);
    });
    QObject::connect(removeBtn, &QPushButton::clicked, dlg, [dlg, tableWidget, statsLabel, searchEdit]() {
        const QList<int> rows = selectedRowsOf(tableWidget);
        if (rows.isEmpty()) {
            showMessageDialog(dlg, u"删除选中"_s, u"请先选择至少一项。"_s);
            return;
        }
        const QString msg = rows.size() == 1
            ? u"确认删除选中的 1 个程序吗？"_s
            : u"确认删除选中的 %1 个程序吗？"_s.arg(rows.size());
        if (!showMessageDialog(dlg, u"删除选中"_s, msg, true))
            return;
        for (int i = rows.size() - 1; i >= 0; --i)
            tableWidget->removeRow(rows[i]);
        refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
    });
    QObject::connect(removeAllBtn, &QPushButton::clicked, dlg, [dlg, tableWidget, statsLabel, searchEdit]() {
        if (tableWidget->rowCount() == 0) {
            showMessageDialog(dlg, u"删除全部"_s, u"当前列表为空，无需删除。"_s);
            return;
        }
        if (!showMessageDialog(dlg, u"删除全部"_s, u"确认删除列表中的全部程序吗？"_s, true))
            return;
        tableWidget->clearContents();
        tableWidget->setRowCount(0);
        refreshWhitelistFilter(tableWidget, statsLabel, searchEdit->text());
    });

    auto* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    auto* okBtn = new QPushButton(u"确定"_s, dlg);
    auto* cancelBtn = new QPushButton(u"取消"_s, dlg);
    QObject::connect(okBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);

    QObject::connect(dlg, &QDialog::accepted, dlg, [this, tableWidget]() {
        duplicateWhitelist = collectWhitelistPaths(tableWidget);
        ConfigDatabase::instance().setValue(u"duplicateWhitelist"_s, duplicateWhitelist.join(u"|"_s));
    });
    QObject::connect(dlg, &QObject::destroyed, this, []() {
        s_dialog = nullptr;
    });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
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
