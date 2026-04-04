#include "SuperGuardian.h"
#include "AppStorage.h"
#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include "DialogHelpers.h"
#include "LogDatabase.h"
#include "ConfigDatabase.h"
#include <QtWidgets>
#include <QDesktopServices>
#include <QUrl>
#include <windows.h>

// ---- 主窗口核心：构造、析构、窗口事件 ----

SuperGuardian::SuperGuardian(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(u"超级守护"_s);
    setWindowIcon(QIcon(u":/SuperGuardian/app.ico"_s));
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
    lineEdit->setPlaceholderText(u"输入文件完整路径；支持鼠标拖放文件到此处；系统内置工具（如 PowerShell）可仅输入名称；支持携带参数；支持识别快捷方式"_s);
    int lineH = lineEdit->fontMetrics().height();
    int inputH = (lineH * 2 + 10) * 3 / 4;
    lineEdit->setMinimumHeight(inputH);
    btnBrowse = new QPushButton(u"选择程序"_s, this);
    btnCancel = new QPushButton(u"取消"_s, this);
    btnAdd = new QPushButton(u"添加"_s, this);
    btnBrowse->setFixedHeight(inputH);
    btnCancel->setFixedHeight(inputH);
    btnAdd->setFixedHeight(inputH);
    btnCancel->setEnabled(false);
    btnAdd->setEnabled(false);
    btnAdd->setObjectName("primaryBtn");
    tableWidget = new DesktopSelectTable(this);

    tableWidget->setColumnCount(10);
    tableWidget->setHorizontalHeaderLabels({ u"程序"_s, u"运行状态"_s, u"持续运行时长"_s, u"上次重启(运行)"_s, u"被守护次数"_s, u"持续守护时长"_s, u"定时规则"_s, u"下次重启(运行)"_s, u"启动延时"_s, u"操作"_s });
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
        hdr->setToolTip(u"程序重启时的启动延时，单位为秒。\n默认 1 秒，可设置为 0 关闭延时。\n守护重启、定时重启均使用此延时。\n定时运行不使用此项。"_s);
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
    selfGuardAct = trayMenu->addAction(u"自我守护"_s);
    selfGuardAct->setCheckable(true);
    autostartAct = trayMenu->addAction(u"开机自启"_s);
    autostartAct->setCheckable(true);
    trayEmailAct = trayMenu->addAction(u"邮件提醒"_s);
    trayEmailAct->setCheckable(true);
    minimizeToTrayAct = trayMenu->addAction(u"启动时最小化到托盘"_s);
    minimizeToTrayAct->setCheckable(true);
    trayMenu->addSeparator();
    QAction* exitAct = trayMenu->addAction(u"退出"_s, this, &SuperGuardian::onExit);
    tray->setContextMenu(trayMenu);
    tray->show();

    // ---- 菜单栏：查看 - 选项 - 配置 - 操作 - 测试 ----
    QMenu* viewMenu = menuBar()->addMenu(u"查看"_s);
    viewMenu->addAction(u"操作日志"_s, this, &SuperGuardian::showOperationLog);
    viewMenu->addAction(u"软件运行日志"_s, this, &SuperGuardian::showRuntimeLog);
    viewMenu->addSeparator();
    viewMenu->addAction(u"守护日志"_s, this, &SuperGuardian::showGuardLog);
    viewMenu->addAction(u"定时重启日志"_s, this, &SuperGuardian::showScheduledRestartLog);
    viewMenu->addAction(u"定时运行日志"_s, this, &SuperGuardian::showScheduledRunLog);

    QMenu* optionsMenu = menuBar()->addMenu(u"选项"_s);
    optionsMenu->addAction(selfGuardAct);
    optionsMenu->addAction(autostartAct);
    emailEnabledAct = optionsMenu->addAction(u"邮件提醒"_s);
    emailEnabledAct->setCheckable(true);
    emailEnabledAct->setChecked(false);
    optionsMenu->addAction(minimizeToTrayAct);

    QMenu* configMenu = menuBar()->addMenu(u"配置"_s);
    configMenu->addAction(u"导入"_s, this, &SuperGuardian::importConfig);
    configMenu->addAction(u"导出"_s, this, &SuperGuardian::exportConfig);
    configMenu->addAction(u"邮件提醒配置"_s, this, &SuperGuardian::showSmtpConfigDialog);
    configMenu->addAction(u"允许重复添加的程序"_s, this, &SuperGuardian::showDuplicateWhitelistDialog);
    configMenu->addSeparator();
    configMenu->addAction(u"重置全部配置"_s, this, &SuperGuardian::resetConfig);

    QMenu* operationMenu = menuBar()->addMenu(u"操作"_s);
    operationMenu->addAction(u"清空列表"_s, this, &SuperGuardian::clearListWithConfirmation);
    operationMenu->addAction(u"重置列表所有列宽"_s, this, &SuperGuardian::resetColumnWidths);
    operationMenu->addAction(u"重置表头显示"_s, this, &SuperGuardian::resetHeaderDisplay);
    operationMenu->addSeparator();
    operationMenu->addAction(u"关闭所有守护"_s, this, &SuperGuardian::closeAllGuards);
    operationMenu->addAction(u"关闭所有定时重启"_s, this, &SuperGuardian::closeAllScheduledRestart);
    operationMenu->addAction(u"关闭所有定时运行"_s, this, &SuperGuardian::closeAllScheduledRun);
    operationMenu->addSeparator();
    operationMenu->addAction(u"移动软件窗口到居中位置"_s, this, &SuperGuardian::centerWindow);
    operationMenu->addAction(u"添加桌面快捷方式"_s, this, &SuperGuardian::createDesktopShortcut);

    QMenu* testMenu = menuBar()->addMenu(u"测试"_s);
    testMenu->addAction(u"测试自我守护"_s, this, &SuperGuardian::runSelfGuardTest);
    testMenu->addAction(u"测试程序是否允许重复添加"_s, this, &SuperGuardian::testDuplicateAdd);

    QMenu* helpMenu = menuBar()->addMenu(u"帮助"_s);
    helpMenu->addAction(u"更新"_s, this, &SuperGuardian::showUpdateDialog);
    helpMenu->addAction(u"导出诊断信息"_s, this, &SuperGuardian::exportDiagnosticInfo);
    helpMenu->addSeparator();
    helpMenu->addAction(u"关于 超级守护"_s, this, &SuperGuardian::showAboutDialog);

    // ---- 置顶按钮 + 主题切换按钮（菜单栏右侧角落） ----
    pinToggleBtn = new QToolButton(this);
    pinToggleBtn->setObjectName("pinToggleBtn");
    pinToggleBtn->setAutoRaise(true);
    pinToggleBtn->setCheckable(true);
    pinToggleBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    pinToggleBtn->setCursor(Qt::PointingHandCursor);
    pinToggleBtn->setIconSize(QSize(20, 20));
    pinToggleBtn->setFixedSize(28, 28);
    pinToggleBtn->setToolTip(u"置顶窗口"_s);
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
    dlg.setWindowTitle(u"关于 超级守护"_s);
    dlg.setFixedSize(420, 320);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QLabel* iconLabel = new QLabel();
    iconLabel->setPixmap(QIcon(u":/SuperGuardian/app.ico"_s).pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(iconLabel);
    QLabel* nameLabel = new QLabel(u"超级守护"_s);
    nameLabel->setAlignment(Qt::AlignCenter);
    QFont f = nameLabel->font();
    f.setPointSize(16);
    f.setBold(true);
    nameLabel->setFont(f);
    lay->addWidget(nameLabel);
    QLabel* verLabel = new QLabel(u"版本 v"_s + QCoreApplication::applicationVersion());
    verLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(verLabel);
    QLabel* descLabel = new QLabel(u"Windows 进程守护与定时管理工具"_s);
    descLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(descLabel);
    QLabel* openSourceLabel = new QLabel(u"本软件完全开源"_s);
    openSourceLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(openSourceLabel);
    QLabel* linkLabel = new QLabel(u"<a href=\"https://github.com/Mrluy/SuperGuardian/tree/master\">项目地址：https://github.com/Mrluy/SuperGuardian/tree/master</a>"_s);
    linkLabel->setAlignment(Qt::AlignCenter);
    linkLabel->setOpenExternalLinks(true);
    linkLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    lay->addWidget(linkLabel);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnLay->addWidget(okBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    dlg.exec();
}

SuperGuardian::~SuperGuardian()
{
}
