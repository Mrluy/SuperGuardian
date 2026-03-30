#include "SuperGuardian.h"
#include "AppStorage.h"
#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include "DialogHelpers.h"
#include <QtWidgets>
#include <QDesktopServices>
#include <QUrl>
#include <windows.h>

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
    tableWidget->setHorizontalHeaderLabels({ QString::fromUtf8("\u7a0b\u5e8f"), QString::fromUtf8("\u8fd0\u884c\u72b6\u6001"), QString::fromUtf8("\u6301\u7eed\u8fd0\u884c\u65f6\u957f"), QString::fromUtf8("上次重启/运行"), QString::fromUtf8("\u5b88\u62a4\u6b21\u6570"), QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f/\u8fd0\u884c\u89c4\u5219"), QString::fromUtf8("下次重启/运行"), QString::fromUtf8("\u542f\u52a8\u5ef6\u65f6"), QString::fromUtf8("\u64cd\u4f5c") });
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

    QMenu* aboutMenu = menuBar()->addMenu(QString::fromUtf8("\u5173\u4e8e"));
    aboutMenu->addAction(QString::fromUtf8("\u66f4\u65b0"), this, &SuperGuardian::showUpdateDialog);

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
    connect(btnAdd, &QPushButton::clicked, this, &SuperGuardian::parseAndAddFromInput);
    connect(lineEdit, &QLineEdit::returnPressed, this, &SuperGuardian::parseAndAddFromInput);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SuperGuardian::checkProcesses);
    timer->start(2000);

    connect(tableWidget, &QTableWidget::cellDoubleClicked, this, &SuperGuardian::onTableDoubleClicked);
    connect(tray, &QSystemTrayIcon::activated, this, &SuperGuardian::onTrayActivated);
    static_cast<DesktopSelectTable*>(tableWidget)->onRowMoved = [this](int from, int to) { handleRowMoved(from, to); };

    // 表头排序（点击列标题循环：升序→降序→默认）
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section == 8) return;
        QHeaderView* header = tableWidget->horizontalHeader();
        if (activeSortSection == section && sortState == 2) {
            sortState = 0;
            activeSortSection = -1;
            header->setSortIndicatorShown(false);
        } else if (activeSortSection == section && sortState == 1) {
            sortState = 2;
        } else {
            sortState = 1;
            activeSortSection = section;
        }
        performSort();
        saveSortState();
    });

    // 表头右键菜单：选择显示/隐藏列
    tableWidget->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tableWidget->horizontalHeader(), &QHeaderView::customContextMenuRequested,
        this, &SuperGuardian::onHeaderContextMenu);

    loadSettings();
    applySavedTrayOptions();
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QString theme = s.contains("theme") ? s.value("theme").toString() : detectSystemThemeName();
    if (!s.contains("theme")) s.setValue("theme", theme);
    applyTheme(theme);

    // 恢复排序状态
    activeSortSection = s.value("sortSection", -1).toInt();
    sortState = s.value("sortState", 0).toInt();
    if (sortState != 0 && activeSortSection >= 0 && activeSortSection < 8)
        performSort();

    restoreColumnVisibility();

    // 列宽调整：操作列固定在最右侧，未调整的列按原占比重新分配
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logicalIndex, int, int newSize) {
        if (autoResizingColumns || logicalIndex >= 8) return;
        autoResizingColumns = true;
        int col8w = tableWidget->columnWidth(8);
        int available = tableWidget->viewport()->width() - col8w;
        int remaining = available - newSize;
        QVector<int> others;
        double othersTotal = 0;
        for (int i = 0; i < 8; i++) {
            if (i == logicalIndex || tableWidget->isColumnHidden(i)) continue;
            others.append(i);
            othersTotal += tableWidget->columnWidth(i);
        }
        if (others.isEmpty() || othersTotal <= 0 || remaining < others.size() * 40) {
            autoResizingColumns = false;
            return;
        }
        int distributed = 0;
        for (int k = 0; k < others.size() - 1; k++) {
            int w = qMax(40, (int)(remaining * tableWidget->columnWidth(others[k]) / othersTotal));
            tableWidget->setColumnWidth(others[k], w);
            distributed += w;
        }
        tableWidget->setColumnWidth(others.last(), qMax(40, remaining - distributed));
        saveColumnWidths();
        autoResizingColumns = false;
    });
}

SuperGuardian::~SuperGuardian()
{
}
