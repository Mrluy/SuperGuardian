#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "LogDatabase.h"
#include <QtWidgets>

using namespace Qt::Literals::StringLiterals;

// ---- 日志查看器通用实现 ----

static void showLogViewer(QWidget* parent, const QString& title, const QString& category) {
    QDialog dlg(parent, kDialogFlags | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    dlg.setWindowTitle(title);
    dlg.resize(900, 600);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    // 搜索栏
    QHBoxLayout* searchLay = new QHBoxLayout();
    QLineEdit* searchEdit = new QLineEdit();
    searchEdit->setPlaceholderText(u"搜索日志内容..."_s);
    searchLay->addWidget(searchEdit);
    QPushButton* refreshBtn = new QPushButton(u"刷新"_s);
    searchLay->addWidget(refreshBtn);
    QPushButton* clearBtn = new QPushButton(u"清空日志"_s);
    searchLay->addWidget(clearBtn);
    lay->addLayout(searchLay);

    // 日志表格
    QTableWidget* table = new QTableWidget();
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
    lay->addWidget(table);

    // 分页
    QHBoxLayout* pageLay = new QHBoxLayout();
    QLabel* infoLabel = new QLabel();
    pageLay->addWidget(infoLabel);
    pageLay->addStretch();
    QPushButton* prevBtn = new QPushButton(u"上一页"_s);
    QPushButton* nextBtn = new QPushButton(u"下一页"_s);
    pageLay->addWidget(prevBtn);
    pageLay->addWidget(nextBtn);
    lay->addLayout(pageLay);

    int pageSize = 500;
    int currentPage = 0;

    auto loadPage = [&]() {
        table->setRowCount(0);
        QString filter = searchEdit->text().trimmed();
        int total = LogDatabase::instance().logCount(category);
        QList<LogEntry> entries = LogDatabase::instance().queryLogs(category, pageSize, currentPage * pageSize);

        // 过滤
        QList<LogEntry> filtered;
        if (filter.isEmpty()) {
            filtered = entries;
        } else {
            for (const LogEntry& e : entries) {
                if (e.message.contains(filter, Qt::CaseInsensitive) ||
                    e.program.contains(filter, Qt::CaseInsensitive)) {
                    filtered.append(e);
                }
            }
        }

        table->setRowCount(filtered.size());
        for (int i = 0; i < filtered.size(); ++i) {
            const LogEntry& e = filtered[i];
            table->setItem(i, 0, new QTableWidgetItem(e.timestamp.toString(u"yyyy-MM-dd hh:mm:ss.zzz"_s)));
            auto categoryLabel = [&](const QString& cat) -> QString {
                if (cat == u"operation"_s) return u"操作"_s;
                if (cat == u"runtime"_s) return u"运行"_s;
                if (cat == u"guard"_s) return u"守护"_s;
                if (cat == u"scheduled_restart"_s) return u"定时重启"_s;
                if (cat == u"scheduled_run"_s) return u"定时运行"_s;
                return cat;
            };
            table->setItem(i, 1, new QTableWidgetItem(categoryLabel(category)));
            table->setItem(i, 2, new QTableWidgetItem(e.program));
            table->setItem(i, 3, new QTableWidgetItem(e.message));
        }

        int totalPages = qMax(1, (total + pageSize - 1) / pageSize);
        infoLabel->setText(u"共 %1 条记录，第 %2/%3 页"_s
            .arg(total).arg(currentPage + 1).arg(totalPages));
        prevBtn->setEnabled(currentPage > 0);
        nextBtn->setEnabled((currentPage + 1) < totalPages);
    };

    QObject::connect(refreshBtn, &QPushButton::clicked, &dlg, [&]() {
        currentPage = 0;
        loadPage();
    });

    QObject::connect(searchEdit, &QLineEdit::returnPressed, &dlg, [&]() {
        currentPage = 0;
        loadPage();
    });

    QObject::connect(clearBtn, &QPushButton::clicked, &dlg, [&]() {
        if (showMessageDialog(&dlg, u"清空日志"_s, u"确认清空所有日志吗？"_s, true)) {
            LogDatabase::instance().clearLogs(category);
            currentPage = 0;
            loadPage();
        }
    });

    QObject::connect(prevBtn, &QPushButton::clicked, &dlg, [&]() {
        if (currentPage > 0) { --currentPage; loadPage(); }
    });

    QObject::connect(nextBtn, &QPushButton::clicked, &dlg, [&]() {
        ++currentPage;
        loadPage();
    });

    loadPage();
    dlg.exec();
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
