#include "SuperGuardian.h"
#include "AppStorage.h"
#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include "DialogHelpers.h"
#include <QtWidgets>
#include <QActionGroup>
#include <QDesktopServices>
#include <QUrl>
#include <QAbstractSpinBox>
#include <windows.h>

// ---- 全局中文右键菜单过滤器 ----

class ChineseContextMenuFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() != QEvent::ContextMenu) return false;
        QLineEdit* le = qobject_cast<QLineEdit*>(obj);
        QAbstractSpinBox* sb = nullptr;
        if (!le) {
            sb = qobject_cast<QAbstractSpinBox*>(obj);
            if (sb) le = sb->findChild<QLineEdit*>();
        }
        if (!le) return false;
        QContextMenuEvent* ce = static_cast<QContextMenuEvent*>(event);
        QMenu* m = le->createStandardContextMenu();
        if (!m) return false;
        for (QAction* a : m->actions()) {
            if (a->isSeparator()) continue;
            QString t = a->text();
            int tab = t.indexOf(QLatin1Char('\t'));
            QString label = tab >= 0 ? t.left(tab) : t;
            QString shortcut = tab >= 0 ? t.mid(tab) : QString();
            if (label == "&Undo") label = QString::fromUtf8("\u64a4\u9500");
            else if (label == "&Redo") label = QString::fromUtf8("\u91cd\u505a");
            else if (label == "Cu&t") label = QString::fromUtf8("\u526a\u5207");
            else if (label == "&Copy") label = QString::fromUtf8("\u590d\u5236");
            else if (label == "&Paste") label = QString::fromUtf8("\u7c98\u8d34");
            else if (label == "Delete") label = QString::fromUtf8("\u5220\u9664");
            else if (label == "Select All") label = QString::fromUtf8("\u5168\u9009");
            else continue;
            a->setText(label + shortcut);
        }
        if (!sb) sb = qobject_cast<QAbstractSpinBox*>(le->parentWidget());
        if (sb) {
            m->addSeparator();
            QAction* upAct = m->addAction(QString::fromUtf8("\u589e\u5927"));
            QAction* downAct = m->addAction(QString::fromUtf8("\u51cf\u5c0f"));
            QObject::connect(upAct, &QAction::triggered, sb, &QAbstractSpinBox::stepUp);
            QObject::connect(downAct, &QAction::triggered, sb, &QAbstractSpinBox::stepDown);
        }
        m->exec(ce->globalPos());
        delete m;
        return true;
    }
};

// ---- 主窗口核心：构造、析构、窗口事件 ----

SuperGuardian::SuperGuardian(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromUtf8("超级守护"));
    setWindowIcon(QIcon(":/SuperGuardian/app.ico"));
    resize(1280, 720);

    // ---- UI 控件 ----
    qApp->installEventFilter(new ChineseContextMenuFilter(this));

    class PathLineEdit : public QLineEdit {
    public:
        PathLineEdit(QWidget* p=nullptr) : QLineEdit(p) { setAcceptDrops(true); }
    protected:
        void dragEnterEvent(QDragEnterEvent* e) override { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
        void dropEvent(QDropEvent* e) override { const QList<QUrl> urls = e->mimeData()->urls(); if (!urls.isEmpty()) setText(urls.first().toLocalFile()); }
    };

    lineEdit = new PathLineEdit(this);
    lineEdit->setPlaceholderText(QString::fromUtf8("输入文件完整路径；支持鼠标拖放文件到此处；系统内置工具（如 PowerShell）可仅输入名称；支持携带参数；支持识别快捷方式"));
    int lineH = lineEdit->fontMetrics().height();
    int inputH = (lineH * 2 + 10) * 3 / 4;
    lineEdit->setMinimumHeight(inputH);
    btnBrowse = new QPushButton(QString::fromUtf8("选择程序"), this);
    btnCancel = new QPushButton(QString::fromUtf8("取消"), this);
    btnAdd = new QPushButton(QString::fromUtf8("添加"), this);
    btnBrowse->setFixedHeight(inputH);
    btnCancel->setFixedHeight(inputH);
    btnAdd->setFixedHeight(inputH);
    btnCancel->setEnabled(false);
    btnAdd->setEnabled(false);
    tableWidget = new DesktopSelectTable(this);

    tableWidget->setColumnCount(9);
    tableWidget->setHorizontalHeaderLabels({ QString::fromUtf8("\u7a0b\u5e8f"), QString::fromUtf8("\u8fd0\u884c\u72b6\u6001"), QString::fromUtf8("\u6301\u7eed\u8fd0\u884c\u65f6\u957f"), QString::fromUtf8("上次重启/运行"), QString::fromUtf8("\u5b88\u62a4\u6b21\u6570"), QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u89c4\u5219"), QString::fromUtf8("下次重启/运行"), QString::fromUtf8("\u542f\u52a8\u5ef6\u65f6"), QString::fromUtf8("\u64cd\u4f5c") });
    tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    tableWidget->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Fixed);
    tableWidget->setColumnWidth(8, 300);
    tableWidget->setSortingEnabled(false);
    tableWidget->horizontalHeader()->setSortIndicatorShown(false);
    tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableWidget->setFocusPolicy(Qt::ClickFocus);
    tableWidget->setDragDropMode(QAbstractItemView::NoDragDrop);
    tableWidget->setDragEnabled(false);
    tableWidget->setItemDelegate(new BruteForceDelegate(tableWidget));
    tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tableWidget, &QTableWidget::customContextMenuRequested, this, &SuperGuardian::onTableContextMenuRequested);
    if (QTableWidgetItem* hdr = tableWidget->horizontalHeaderItem(7)) {
        hdr->setToolTip(QString::fromUtf8("\u7a0b\u5e8f\u91cd\u542f\u65f6\u7684\u542f\u52a8\u5ef6\u65f6\uff0c\u5355\u4f4d\u4e3a\u79d2\u3002\n\u9ed8\u8ba4 1 \u79d2\uff0c\u53ef\u8bbe\u7f6e\u4e3a 0 \u5173\u95ed\u5ef6\u65f6\u3002\n\u5b88\u62a4\u91cd\u542f\u3001\u5b9a\u65f6\u91cd\u542f\u5747\u4f7f\u7528\u6b64\u5ef6\u65f6\u3002\n\u5b9a\u65f6\u8fd0\u884c\u4e0d\u4f7f\u7528\u6b64\u9879\u3002"));
    }
    tableWidget->verticalHeader()->setSectionsClickable(false);
    tableWidget->verticalHeader()->setHighlightSections(false);
    tableWidget->verticalHeader()->setFocusPolicy(Qt::NoFocus);
    tableWidget->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    tableWidget->verticalHeader()->hide();
    tableWidget->setMouseTracking(true);

    QWidget* top = new QWidget(this);
    QHBoxLayout* topLayout = new QHBoxLayout(top);
    topLayout->setAlignment(Qt::AlignVCenter);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->addWidget(lineEdit);
    topLayout->addWidget(btnBrowse);
    topLayout->addWidget(btnCancel);
    topLayout->addWidget(btnAdd);

    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->addWidget(top);
    mainLayout->addWidget(tableWidget);
    setCentralWidget(central);

    // ---- 托盘（精简：自我守护、开机自启、退出） ----
    tray = new QSystemTrayIcon(this);
    tray->setIcon(QIcon(":/SuperGuardian/app.ico"));
    trayMenu = new QMenu();
    selfGuardAct = trayMenu->addAction(QString::fromUtf8("自我守护"));
    selfGuardAct->setCheckable(true);
    autostartAct = trayMenu->addAction(QString::fromUtf8("开机自启"));
    autostartAct->setCheckable(true);
    trayEmailAct = trayMenu->addAction(QString::fromUtf8("邮件提醒"));
    trayEmailAct->setCheckable(true);
    minimizeToTrayAct = trayMenu->addAction(QString::fromUtf8("\u542f\u52a8\u65f6\u6700\u5c0f\u5316\u5230\u6258\u76d8"));
    minimizeToTrayAct->setCheckable(true);
    trayMenu->addSeparator();
    QAction* exitAct = trayMenu->addAction(QString::fromUtf8("退出"), this, &SuperGuardian::onExit);
    tray->setContextMenu(trayMenu);
    tray->show();

    // ---- 菜单栏：查看 - 选项 - 配置 - 操作 - 测试 ----
    QMenu* viewMenu = menuBar()->addMenu(QString::fromUtf8("查看"));
    viewMenu->addAction(QString::fromUtf8("守护日志"), this, []() {
        QString path = watchdogLogFilePath();
        if (path.isEmpty()) return;
        if (!QFile::exists(path)) { QFile f(path); f.open(QIODevice::WriteOnly); f.close(); }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    viewMenu->addAction(QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u65e5\u5fd7"), this, []() {
        QString path = scheduledRestartLogFilePath();
        if (path.isEmpty()) return;
        if (!QFile::exists(path)) { QFile f(path); f.open(QIODevice::WriteOnly); f.close(); }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    viewMenu->addAction(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c\u65e5\u5fd7"), this, []() {
        QString path = scheduledRunLogFilePath();
        if (path.isEmpty()) return;
        if (!QFile::exists(path)) { QFile f(path); f.open(QIODevice::WriteOnly); f.close(); }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    QMenu* optionsMenu = menuBar()->addMenu(QString::fromUtf8("\u9009\u9879"));
    optionsMenu->addAction(selfGuardAct);
    optionsMenu->addAction(autostartAct);
    emailEnabledAct = optionsMenu->addAction(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192"));
    emailEnabledAct->setCheckable(true);
    emailEnabledAct->setChecked(false);
    optionsMenu->addAction(minimizeToTrayAct);

    QMenu* configMenu = menuBar()->addMenu(QString::fromUtf8("\u914d\u7f6e"));
    configMenu->addAction(QString::fromUtf8("\u5bfc\u5165"), this, &SuperGuardian::importConfig);
    configMenu->addAction(QString::fromUtf8("\u5bfc\u51fa"), this, &SuperGuardian::exportConfig);
    configMenu->addAction(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192\u914d\u7f6e"), this, &SuperGuardian::showSmtpConfigDialog);
    configMenu->addSeparator();
    configMenu->addAction(QString::fromUtf8("\u91cd\u7f6e\u5168\u90e8\u914d\u7f6e"), this, &SuperGuardian::resetConfig);

    QMenu* operationMenu = menuBar()->addMenu(QString::fromUtf8("操作"));
    operationMenu->addAction(QString::fromUtf8("清空列表"), this, &SuperGuardian::clearListWithConfirmation);
    operationMenu->addAction(QString::fromUtf8("重置列宽"), this, &SuperGuardian::resetColumnWidths);
    operationMenu->addSeparator();
    operationMenu->addAction(QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b88\u62a4"), this, &SuperGuardian::closeAllGuards);
    operationMenu->addAction(QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b9a\u65f6\u91cd\u542f"), this, &SuperGuardian::closeAllScheduledRestart);
    operationMenu->addAction(QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b9a\u65f6\u8fd0\u884c"), this, &SuperGuardian::closeAllScheduledRun);
    operationMenu->addSeparator();
    operationMenu->addAction(QString::fromUtf8("添加桌面快捷方式"), this, &SuperGuardian::createDesktopShortcut);

    QMenu* testMenu = menuBar()->addMenu(QString::fromUtf8("测试"));
    testMenu->addAction(QString::fromUtf8("测试自我守护"), this, &SuperGuardian::runSelfGuardTest);

    // ---- 主题切换按钮（菜单栏右侧角落） ----
    themeToggleBtn = new QToolButton(this);
    themeToggleBtn->setObjectName("themeToggleBtn");
    themeToggleBtn->setAutoRaise(true);
    themeToggleBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    themeToggleBtn->setCursor(Qt::PointingHandCursor);
    themeToggleBtn->setIconSize(QSize(20, 20));
    themeToggleBtn->setFixedSize(28, 28);
    connect(themeToggleBtn, &QToolButton::clicked, this, &SuperGuardian::toggleTheme);
    menuBar()->setCornerWidget(themeToggleBtn, Qt::TopRightCorner);

    // ---- 信号连接 ----
    connect(selfGuardAct, &QAction::toggled, this, &SuperGuardian::onSelfGuardToggled);
    connect(autostartAct, &QAction::toggled, this, &SuperGuardian::onAutostartToggled);

    // 同步托盘邮件提醒与菜单栏邮件提醒
    connect(emailEnabledAct, &QAction::toggled, this, [this](bool on) {
        trayEmailAct->blockSignals(true);
        trayEmailAct->setChecked(on);
        trayEmailAct->blockSignals(false);
        QSettings s(appSettingsFilePath(), QSettings::IniFormat);
        s.setValue("emailEnabled", on);
    });
    connect(trayEmailAct, &QAction::toggled, this, [this](bool on) {
        emailEnabledAct->blockSignals(true);
        emailEnabledAct->setChecked(on);
        emailEnabledAct->blockSignals(false);
        QSettings s(appSettingsFilePath(), QSettings::IniFormat);
        s.setValue("emailEnabled", on);
    });

    connect(minimizeToTrayAct, &QAction::toggled, this, [this](bool on) {
        QSettings s(appSettingsFilePath(), QSettings::IniFormat);
        s.setValue("minimizeToTray", on);
    });

    connect(btnBrowse, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, QString::fromUtf8("选择程序"), "", "Executable (*.exe);;All Files (*)");
        if (!file.isEmpty()) lineEdit->setText(file);
    });
    connect(btnCancel, &QPushButton::clicked, this, [this]() { lineEdit->clear(); });
    connect(lineEdit, &QLineEdit::textChanged, this, [this](const QString& t){
        bool hasText = !t.trimmed().isEmpty();
        btnAdd->setEnabled(hasText);
        btnCancel->setEnabled(hasText);
    });
    auto doAddProgram = [this]() {
        QString text = lineEdit->text().trimmed();
        if (text.isEmpty()) return;
        QString progPath, progArgs;
        if (text.startsWith('"')) {
            int cq = text.indexOf('"', 1);
            if (cq > 0) { progPath = text.mid(1, cq - 1); progArgs = text.mid(cq + 1).trimmed(); }
            else progPath = text.mid(1);
        } else if (QFileInfo::exists(text)) {
            progPath = text;
        } else {
            bool found = false;
            int searchFrom = 0;
            while (searchFrom < text.length()) {
                int sp = text.indexOf(' ', searchFrom);
                if (sp < 0) break;
                QString cand = text.left(sp);
                if (QFileInfo::exists(cand) || !QStandardPaths::findExecutable(cand).isEmpty()) {
                    progPath = cand; progArgs = text.mid(sp + 1).trimmed(); found = true; break;
                }
                searchFrom = sp + 1;
            }
            if (!found) progPath = text;
        }
        addProgram(progPath.trimmed(), progArgs.trimmed());
        lineEdit->clear();
    };
    connect(btnAdd, &QPushButton::clicked, this, doAddProgram);
    connect(lineEdit, &QLineEdit::returnPressed, this, doAddProgram);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SuperGuardian::checkProcesses);
    timer->start(2000);

    connect(tableWidget, &QTableWidget::cellDoubleClicked, this, &SuperGuardian::onTableDoubleClicked);
    connect(tray, &QSystemTrayIcon::activated, this, &SuperGuardian::onTrayActivated);
    static_cast<DesktopSelectTable*>(tableWidget)->onRowMoved = [this](int from, int to) { handleRowMoved(from, to); };
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section == 8) return;

        QHeaderView* header = tableWidget->horizontalHeader();
        if (header->sortIndicatorSection() == section && sortState == 2) {
            sortState = 0;
            header->setSortIndicatorShown(false);
            rebuildTableFromItems();
        } else {
            Qt::SortOrder order;
            if (header->sortIndicatorSection() == section && sortState == 1) {
                sortState = 2;
                order = Qt::DescendingOrder;
            } else {
                sortState = 1;
                order = Qt::AscendingOrder;
            }
            header->setSortIndicatorShown(true);
            header->setSortIndicator(section, order);

            // Pin-aware sort: pinned items sort among themselves at top,
            // unpinned items sort among themselves below
            auto collectRows = [&](bool pinned) -> QVector<QPair<QString, int>> {
                QVector<QPair<QString, int>> rows;
                for (int r = 0; r < tableWidget->rowCount(); r++) {
                    int idx = findItemIndexByPath(rowPath(r));
                    if (idx < 0) continue;
                    if (items[idx].pinned != pinned) continue;
                    QTableWidgetItem* it = tableWidget->item(r, section);
                    rows.append({ it ? it->text() : QString(), idx });
                }
                return rows;
            };
            auto sortRows = [&](QVector<QPair<QString, int>>& rows) {
                std::sort(rows.begin(), rows.end(), [order](const QPair<QString, int>& a, const QPair<QString, int>& b) {
                    return (order == Qt::AscendingOrder) ? (a.first.localeAwareCompare(b.first) < 0)
                                                        : (a.first.localeAwareCompare(b.first) > 0);
                });
            };

            auto pinnedRows = collectRows(true);
            auto unpinnedRows = collectRows(false);
            sortRows(pinnedRows);
            sortRows(unpinnedRows);

            QVector<GuardItem> newItems;
            for (const auto& p : pinnedRows) newItems.append(items[p.second]);
            for (const auto& p : unpinnedRows) newItems.append(items[p.second]);
            items = newItems;
            rebuildTableFromItems();
        }
    });

    loadSettings();
    applySavedTrayOptions();
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QString theme = s.contains("theme") ? s.value("theme").toString() : detectSystemThemeName();
    if (!s.contains("theme")) s.setValue("theme", theme);
    applyTheme(theme);

    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logicalIndex, int, int) {
        if (logicalIndex < 8 && !autoResizingColumns)
            saveColumnWidths();
    });
}

SuperGuardian::~SuperGuardian()
{
}

void SuperGuardian::toggleVisible() {
    if (isVisible() && !(windowState() & Qt::WindowMinimized)) {
        hide();
    } else {
        showNormal();
        raise();
        activateWindow();
    }
}

void SuperGuardian::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) toggleVisible();
}

void SuperGuardian::onExit() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    s.setValue("self_guard_manual_exit", true);
    stopWatchdogHelper();
    qApp->quit();
}

void SuperGuardian::closeEvent(QCloseEvent* event) {
    if (tray) {
        hide();
        event->ignore();
    } else {
        QMainWindow::closeEvent(event);
    }
}

void SuperGuardian::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        if (!(windowState() & Qt::WindowMinimized) && isVisible()) {
            show();
        }
    }
    QMainWindow::changeEvent(event);
}

void SuperGuardian::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    distributeColumnWidths();
}

void SuperGuardian::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    static bool firstShow = true;
    if (firstShow) {
        firstShow = false;
        QTimer::singleShot(0, this, &SuperGuardian::distributeColumnWidths);
    }
}

bool SuperGuardian::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    static const UINT WM_SG_SHOW = RegisterWindowMessageW(L"SuperGuardianShowMainWindow");
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_SG_SHOW) {
        showNormal();
        raise();
        activateWindow();
        if (result) *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void SuperGuardian::toggleTheme() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QString current = s.value("theme", "light").toString();
    QString next = (current == "dark") ? "light" : "dark";
    s.setValue("theme", next);
    applyTheme(next);
}

QString SuperGuardian::formatStartDelay(int secs) const {
    if (secs <= 0) return QString::fromUtf8("\u5173\u95ed");
    return QString::number(secs) + QString::fromUtf8(" \u79d2");
}
