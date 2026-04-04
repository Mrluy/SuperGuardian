#include "SuperGuardian.h"
#include "WhitelistData.h"
#include "DialogHelpers.h"
#include "ConfigDatabase.h"
#include "GuardTableWidgets.h"
#include <QtWidgets>
#include <QClipboard>
#include <QMimeData>
#include <QPointer>
#include <QRegularExpression>
#include <memory>

// ---- 允许重复添加的程序白名单对话框 ----

using namespace Qt::Literals::StringLiterals;

namespace {

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
            const QString f = url.toLocalFile();
            if (!f.isEmpty())
                files.append(f);
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

        const int importMode = promptWhitelistImportMode(dlg, u"导入白名单"_s);
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
        const QList<int> rows = selectedWhitelistRows(tableWidget);
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
        const QList<int> rows = selectedWhitelistRows(tableWidget);
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
