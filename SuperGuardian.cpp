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
    btnAdd->setObjectName("primaryBtn");
    tableWidget = new DesktopSelectTable(this);

    tableWidget->setColumnCount(10);
    tableWidget->setHorizontalHeaderLabels({ QString::fromUtf8("\u7a0b\u5e8f"), QString::fromUtf8("\u8fd0\u884c\u72b6\u6001"), QString::fromUtf8("\u6301\u7eed\u8fd0\u884c\u65f6\u957f"), QString::fromUtf8("\u4e0a\u6b21\u91cd\u542f(\u8fd0\u884c)"), QString::fromUtf8("\u88ab\u5b88\u62a4\u6b21\u6570"), QString::fromUtf8("\u6301\u7eed\u5b88\u62a4\u65f6\u957f"), QString::fromUtf8("\u5b9a\u65f6\u89c4\u5219"), QString::fromUtf8("\u4e0b\u6b21\u91cd\u542f(\u8fd0\u884c)"), QString::fromUtf8("\u542f\u52a8\u5ef6\u65f6"), QString::fromUtf8("\u64cd\u4f5c") });
    tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    tableWidget->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Fixed);
    tableWidget->setColumnWidth(9, 300);
    tableWidget->horizontalHeader()->setSectionsMovable(true);
    tableWidget->horizontalHeader()->setHighlightSections(false);
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
    if (QTableWidgetItem* hdr = tableWidget->horizontalHeaderItem(8)) {
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
    topLayout->setSpacing(8);
    topLayout->addWidget(lineEdit);
    topLayout->addWidget(btnBrowse);
    topLayout->addWidget(btnCancel);
    topLayout->addWidget(btnAdd);

    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(12, 8, 12, 12);
    mainLayout->setSpacing(10);
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
    configMenu->addAction(QString::fromUtf8("\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0\u7684\u7a0b\u5e8f"), this, &SuperGuardian::showDuplicateWhitelistDialog);
    configMenu->addSeparator();
    configMenu->addAction(QString::fromUtf8("\u91cd\u7f6e\u5168\u90e8\u914d\u7f6e"), this, &SuperGuardian::resetConfig);

    QMenu* operationMenu = menuBar()->addMenu(QString::fromUtf8("操作"));
    operationMenu->addAction(QString::fromUtf8("清空列表"), this, &SuperGuardian::clearListWithConfirmation);
    operationMenu->addAction(QString::fromUtf8("\u91cd\u7f6e\u5217\u8868\u6240\u6709\u5217\u5bbd"), this, &SuperGuardian::resetColumnWidths);
    operationMenu->addAction(QString::fromUtf8("\u91cd\u7f6e\u8868\u5934\u663e\u793a"), this, &SuperGuardian::resetHeaderDisplay);
    operationMenu->addSeparator();
    operationMenu->addAction(QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b88\u62a4"), this, &SuperGuardian::closeAllGuards);
    operationMenu->addAction(QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b9a\u65f6\u91cd\u542f"), this, &SuperGuardian::closeAllScheduledRestart);
    operationMenu->addAction(QString::fromUtf8("\u5173\u95ed\u6240\u6709\u5b9a\u65f6\u8fd0\u884c"), this, &SuperGuardian::closeAllScheduledRun);
    operationMenu->addSeparator();
    operationMenu->addAction(QString::fromUtf8("\u79fb\u52a8\u8f6f\u4ef6\u7a97\u53e3\u5230\u5c45\u4e2d\u4f4d\u7f6e"), this, &SuperGuardian::centerWindow);
    operationMenu->addAction(QString::fromUtf8("添加桌面快捷方式"), this, &SuperGuardian::createDesktopShortcut);

    QMenu* testMenu = menuBar()->addMenu(QString::fromUtf8("测试"));
    testMenu->addAction(QString::fromUtf8("测试自我守护"), this, &SuperGuardian::runSelfGuardTest);
    testMenu->addAction(QString::fromUtf8("\u6d4b\u8bd5\u7a0b\u5e8f\u662f\u5426\u5141\u8bb8\u91cd\u590d\u6dfb\u52a0"), this, &SuperGuardian::testDuplicateAdd);

    QMenu* helpMenu = menuBar()->addMenu(QString::fromUtf8("\u5e2e\u52a9"));
    helpMenu->addAction(QString::fromUtf8("\u66f4\u65b0"), this, &SuperGuardian::showUpdateDialog);
    helpMenu->addSeparator();
    helpMenu->addAction(QString::fromUtf8("\u5173\u4e8e \u8d85\u7ea7\u5b88\u62a4"), this, &SuperGuardian::showAboutDialog);

    // ---- 置顶按钮 + 主题切换按钮（菜单栏右侧角落） ----
    pinToggleBtn = new QToolButton(this);
    pinToggleBtn->setObjectName("pinToggleBtn");
    pinToggleBtn->setAutoRaise(true);
    pinToggleBtn->setCheckable(true);
    pinToggleBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    pinToggleBtn->setCursor(Qt::PointingHandCursor);
    pinToggleBtn->setIconSize(QSize(20, 20));
    pinToggleBtn->setFixedSize(28, 28);
    pinToggleBtn->setToolTip(QString::fromUtf8("\u7f6e\u9876\u7a97\u53e3"));
    connect(pinToggleBtn, &QToolButton::clicked, this, &SuperGuardian::toggleAlwaysOnTop);

    themeToggleBtn = new QToolButton(this);
    themeToggleBtn->setObjectName("themeToggleBtn");
    themeToggleBtn->setAutoRaise(true);
    themeToggleBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    themeToggleBtn->setCursor(Qt::PointingHandCursor);
    themeToggleBtn->setIconSize(QSize(20, 20));
    themeToggleBtn->setFixedSize(28, 28);
    connect(themeToggleBtn, &QToolButton::clicked, this, &SuperGuardian::toggleTheme);

    QWidget* cornerWidget = new QWidget(this);
    QHBoxLayout* cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(5);
    cornerLayout->addWidget(pinToggleBtn);
    cornerLayout->addWidget(themeToggleBtn);
    menuBar()->setCornerWidget(cornerWidget, Qt::TopRightCorner);

    initSignals();
}

// ---- 关于对话框 ----

void SuperGuardian::showAboutDialog() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u5173\u4e8e \u8d85\u7ea7\u5b88\u62a4"));
    dlg.setFixedSize(360, 260);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QLabel* iconLabel = new QLabel();
    iconLabel->setPixmap(QIcon(":/SuperGuardian/app.ico").pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(iconLabel);
    QLabel* nameLabel = new QLabel(QString::fromUtf8("\u8d85\u7ea7\u5b88\u62a4"));
    nameLabel->setAlignment(Qt::AlignCenter);
    QFont f = nameLabel->font();
    f.setPointSize(16);
    f.setBold(true);
    nameLabel->setFont(f);
    lay->addWidget(nameLabel);
    QLabel* verLabel = new QLabel(QString::fromUtf8("\u7248\u672c v") + QCoreApplication::applicationVersion());
    verLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(verLabel);
    QLabel* descLabel = new QLabel(QString::fromUtf8("Windows \u8fdb\u7a0b\u5b88\u62a4\u4e0e\u5b9a\u65f6\u7ba1\u7406\u5de5\u5177"));
    descLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(descLabel);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnLay->addWidget(okBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    dlg.exec();
}

SuperGuardian::~SuperGuardian()
{
}
