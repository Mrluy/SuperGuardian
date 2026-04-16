#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "GuardTableWidgets.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <memory>

namespace {

class DropLineEdit : public QLineEdit {
public:
    DropLineEdit(QWidget* p = nullptr) : QLineEdit(p) { setAcceptDrops(true); }
protected:
    void dragEnterEvent(QDragEnterEvent* e) override { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
    void dropEvent(QDropEvent* e) override {
        const QList<QUrl> urls = e->mimeData()->urls();
        if (!urls.isEmpty()) setText(QDir::toNativeSeparators(urls.first().toLocalFile()));
    }
};

QString buildProgramDisplayText(const GuardItem& item) {
    if (!item.note.isEmpty())
        return item.note;
    return item.launchArgs.isEmpty()
        ? item.processName
        : item.processName + u" "_s + item.launchArgs;
}

QString buildProgramToolTip(const GuardItem& item) {
    QString name = item.launchArgs.isEmpty()
        ? item.processName
        : item.processName + u" "_s + item.launchArgs;
    return item.id.left(8) + u" "_s + name;
}

void updateProgramCell(QTableWidget* tableWidget, int row, const GuardItem& item) {
    if (QTableWidgetItem* cell = tableWidget->item(row, 1)) {
        cell->setText(buildProgramDisplayText(item));
        cell->setToolTip(buildProgramToolTip(item));
        cell->setIcon(getFileIcon(item.targetPath));
    }
}

}

// ---- 启动延时设置对话框 ----

void SuperGuardian::contextSetNote(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"备注"_s);
    dlg.setFixedWidth(400);
    dlg.setMinimumHeight(140);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"请输入备注名称（留空表示清除备注）："_s));

    QLineEdit* noteEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexById(rowId(rows[0]));
        if (itemIdx >= 0)
            noteEdit->setText(items[itemIdx].note);
    }
    lay->addWidget(noteEdit);
    lay->addStretch();

    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString note = noteEdit->text().trimmed();
    for (int row : rows) {
        int itemIdx = findItemIndexById(rowId(row));
        if (itemIdx < 0)
            continue;

        items[itemIdx].note = note;
        updateProgramCell(tableWidget, row, items[itemIdx]);
    }
    saveSettings();
}

void SuperGuardian::contextSetStartDelay(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"设置启动延时"_s);
    dlg.setFixedWidth(320);
    dlg.setMinimumHeight(150);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"请设置启动延时（秒）："_s));
    lay->addWidget(new QLabel(u"程序重启时的延时，设置为 0 关闭延时。\n守护重启、定时重启均使用此延时。"_s));
    QSpinBox* spin = new QSpinBox();
    spin->setRange(0, 86400);
    spin->setValue(1);
    spin->setSuffix(u" 秒"_s);
    spin->setSpecialValueText(u"关闭"_s);
    if (rows.size() == 1) {
        int itemIdx = findItemIndexById(rowId(rows[0]));
        if (itemIdx >= 0) spin->setValue(items[itemIdx].startDelaySecs);
    }
    lay->addWidget(spin);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    int delaySecs = spin->value();
    for (int row : rows) {
        int itemIdx = findItemIndexById(rowId(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        item.startDelaySecs = delaySecs;
        item.startDelayExitTime = QDateTime();
        logOperation(u"设置启动延时 %1秒"_s.arg(delaySecs), programId(item.processName, item.launchArgs));
        if (tableWidget->item(row, 9)) {
            if (item.scheduledRunEnabled)
                tableWidget->item(row, 9)->setText("-");
            else
                tableWidget->item(row, 9)->setText(formatStartDelay(delaySecs));
        }
    }
    saveSettings();
}

// ---- 启动程序/参数设置对话框 ----

void SuperGuardian::contextSetLaunchArgs(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"设置启动程序/参数"_s);
    dlg.setFixedWidth(450);
    dlg.setMinimumHeight(200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(u"启动程序路径："_s));
    QLineEdit* pathEdit = new DropLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexById(rowId(rows[0]));
        if (itemIdx >= 0) pathEdit->setText(items[itemIdx].targetPath);
    }
    lay->addWidget(pathEdit);

    lay->addWidget(new QLabel(u"启动参数（留空表示无参数）："_s));
    QLineEdit* argsEdit = new DropLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexById(rowId(rows[0]));
        if (itemIdx >= 0) argsEdit->setText(items[itemIdx].launchArgs);
    }
    lay->addWidget(argsEdit);
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

    QString newPath = pathEdit->text().trimmed();
    // 去除外层双引号
    if (newPath.startsWith(u'"') && newPath.endsWith(u'"') && newPath.size() > 2)
        newPath = newPath.mid(1, newPath.size() - 2);
    QString args = argsEdit->text().trimmed();
    for (int row : rows) {
        int itemIdx = findItemIndexById(rowId(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        if (!newPath.isEmpty() && newPath != item.targetPath) {
            QString shortcutArgs;
            item.targetPath = resolveShortcut(newPath, &shortcutArgs);
            item.path = newPath;
            if (args.isEmpty() && !shortcutArgs.isEmpty())
                args = shortcutArgs;
            item.processName = QFileInfo(item.targetPath).fileName();
        }
        item.launchArgs = args;
        logOperation(u"设置启动程序/参数"_s, programId(item.processName, args));
        if (QTableWidgetItem* cell = tableWidget->item(row, 0))
            cell->setData(Qt::UserRole, item.id);
        updateProgramCell(tableWidget, row, item);
    }
    saveSettings();
}

// ---- 日志查看器通用实现 ----

static void showLogViewer(QWidget* parent, const QString& title, const QString& category) {
    const QString logCategory = category;
    auto* dlg = new QDialog(nullptr, kDialogFlags | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowModality(Qt::NonModal);
    dlg->setWindowTitle(title);
    if (parent)
        dlg->setWindowIcon(parent->windowIcon());
    dlg->resize(900, 600);

    QVBoxLayout* lay = new QVBoxLayout(dlg);

    QHBoxLayout* searchLay = new QHBoxLayout();
    QLineEdit* searchEdit = new QLineEdit();
    searchEdit->setPlaceholderText(u"搜索日志内容..."_s);
    searchLay->addWidget(searchEdit);
    QPushButton* refreshBtn = new QPushButton(u"刷新"_s);
    searchLay->addWidget(refreshBtn);
    QPushButton* clearBtn = new QPushButton(u"清空日志"_s);
    searchLay->addWidget(clearBtn);
    lay->addLayout(searchLay);

    auto* table = new DesktopSelectTable(dlg);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({ u"时间"_s, u"类别"_s, u"程序"_s, u"内容"_s });
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->setColumnWidth(2, 200);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->verticalHeader()->setVisible(false);
    table->setWordWrap(false);
    table->setMouseTracking(true);
    table->setRowDragEnabled(false);
    table->setItemDelegate(new BruteForceDelegate(table));
    table->setSortingEnabled(true);
    lay->addWidget(table);

    QHBoxLayout* pageLay = new QHBoxLayout();
    QLabel* infoLabel = new QLabel();
    pageLay->addWidget(infoLabel);
    pageLay->addStretch();
    QPushButton* prevBtn = new QPushButton(u"上一页"_s);
    QPushButton* nextBtn = new QPushButton(u"下一页"_s);
    pageLay->addWidget(prevBtn);
    pageLay->addWidget(nextBtn);
    lay->addLayout(pageLay);

    const int pageSize = 500;
    auto currentPage = std::make_shared<int>(0);

    auto categoryLabel = [](const QString& cat) -> QString {
        if (cat == u"operation"_s) return u"操作"_s;
        if (cat == u"runtime"_s) return u"运行"_s;
        if (cat == u"guard"_s) return u"守护"_s;
        if (cat == u"scheduled_restart"_s) return u"定时重启"_s;
        if (cat == u"scheduled_run"_s) return u"定时运行"_s;
        return cat;
    };

    auto loadPage = std::make_shared<std::function<void()>>();
    *loadPage = [table, searchEdit, infoLabel, prevBtn, nextBtn, currentPage, pageSize, logCategory, categoryLabel]() {
        table->setSortingEnabled(false);
        table->setRowCount(0);
        QString filter = searchEdit->text().trimmed();
        const int total = LogDatabase::instance().logCount(logCategory);

        QList<LogEntry> pageEntries;
        int totalVisible = total;
        if (filter.isEmpty()) {
            pageEntries = LogDatabase::instance().queryLogs(logCategory, pageSize, (*currentPage) * pageSize);
        } else {
            QList<LogEntry> allEntries = LogDatabase::instance().queryLogs(logCategory, total, 0);
            QList<LogEntry> filteredEntries;
            for (const LogEntry& e : allEntries) {
                if (e.message.contains(filter, Qt::CaseInsensitive) ||
                    e.program.contains(filter, Qt::CaseInsensitive)) {
                    filteredEntries.append(e);
                }
            }
            totalVisible = filteredEntries.size();
            const int start = (*currentPage) * pageSize;
            for (int i = start; i < qMin(start + pageSize, totalVisible); ++i)
                pageEntries.append(filteredEntries[i]);
        }

        table->setRowCount(pageEntries.size());
        for (int i = 0; i < pageEntries.size(); ++i) {
            const LogEntry& e = pageEntries[i];
            table->setItem(i, 0, new QTableWidgetItem(e.timestamp.toString(u"yyyy-MM-dd hh:mm:ss.zzz"_s)));
            table->setItem(i, 1, new QTableWidgetItem(categoryLabel(logCategory)));
            table->setItem(i, 2, new QTableWidgetItem(e.program));
            table->setItem(i, 3, new QTableWidgetItem(e.message));
        }

        int totalPages = qMax(1, (totalVisible + pageSize - 1) / pageSize);
        if (*currentPage >= totalPages)
            *currentPage = totalPages - 1;
        infoLabel->setText(filter.isEmpty()
            ? u"共 %1 条记录，第 %2/%3 页"_s.arg(totalVisible).arg(*currentPage + 1).arg(totalPages)
            : u"筛选后 %1/%2 条记录，第 %3/%4 页"_s.arg(totalVisible).arg(total).arg(*currentPage + 1).arg(totalPages));
        prevBtn->setEnabled(*currentPage > 0);
        nextBtn->setEnabled((*currentPage + 1) < totalPages);
        table->setSortingEnabled(true);
    };

    QObject::connect(refreshBtn, &QPushButton::clicked, dlg, [currentPage, loadPage]() {
        *currentPage = 0;
        (*loadPage)();
    });

    QObject::connect(searchEdit, &QLineEdit::returnPressed, dlg, [currentPage, loadPage]() {
        *currentPage = 0;
        (*loadPage)();
    });

    QObject::connect(clearBtn, &QPushButton::clicked, dlg, [dlg, currentPage, loadPage, logCategory]() {
        if (showMessageDialog(dlg, u"清空日志"_s, u"确认清空所有日志吗？"_s, true)) {
            LogDatabase::instance().clearLogs(logCategory);
            *currentPage = 0;
            (*loadPage)();
        }
    });

    QObject::connect(prevBtn, &QPushButton::clicked, dlg, [currentPage, loadPage]() {
        if (*currentPage > 0) {
            --(*currentPage);
            (*loadPage)();
        }
    });

    QObject::connect(nextBtn, &QPushButton::clicked, dlg, [currentPage, loadPage]() {
        ++(*currentPage);
        (*loadPage)();
    });

    (*loadPage)();
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void SuperGuardian::showOperationLog() {
    showLogViewer(this, u"操作日志"_s, u"operation"_s);
}

void SuperGuardian::showRuntimeLog() {
    showLogViewer(this, u"软件运行日志"_s, u"runtime"_s);
}

void SuperGuardian::showGuardLog() {
    showLogViewer(this, u"守护日志"_s, u"guard"_s);
}

void SuperGuardian::showScheduledRestartLog() {
    showLogViewer(this, u"定时重启日志"_s, u"scheduled_restart"_s);
}

void SuperGuardian::showScheduledRunLog() {
    showLogViewer(this, u"定时运行日志"_s, u"scheduled_run"_s);
}
