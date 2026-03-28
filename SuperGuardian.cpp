#include "SuperGuardian.h"
#include "AppStorage.h"
#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include <QtWidgets>
#include <QActionGroup>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

// ---- 主题切换图标生成 ----

static QIcon createSunIcon(int size = 18) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QColor color(220, 160, 0);
    qreal center = size / 2.0;
    qreal bodyR = size / 5.0;
    p.setPen(QPen(color, 1.5));
    for (int i = 0; i < 8; i++) {
        qreal angle = i * M_PI / 4.0;
        qreal inner = bodyR + 1.5;
        qreal outer = center - 1.0;
        p.drawLine(QPointF(center + inner * qCos(angle), center + inner * qSin(angle)),
                   QPointF(center + outer * qCos(angle), center + outer * qSin(angle)));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(center, center), bodyR, bodyR);
    return QIcon(pix);
}

static QIcon createMoonIcon(int size = 18) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QColor color(180, 200, 255);
    qreal center = size / 2.0;
    qreal r = size * 3.0 / 8.0;
    QPainterPath moon;
    moon.addEllipse(QPointF(center, center), r, r);
    QPainterPath cutout;
    cutout.addEllipse(QPointF(center + r * 0.45, center - r * 0.25), r * 0.75, r * 0.75);
    moon -= cutout;
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(moon);
    return QIcon(pix);
}

// ---- 主窗口核心：构造、析构、窗口事件 ----

SuperGuardian::SuperGuardian(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromUtf8("超级守护"));
    setWindowIcon(QIcon(":/SuperGuardian/app.ico"));
    resize(1280, 720);

    // ---- UI 控件 ----
    class PathLineEdit : public QLineEdit {
    public:
        PathLineEdit(QWidget* p=nullptr) : QLineEdit(p) { setAcceptDrops(true); }
    protected:
        void dragEnterEvent(QDragEnterEvent* e) override { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
        void dropEvent(QDropEvent* e) override { const QList<QUrl> urls = e->mimeData()->urls(); if (!urls.isEmpty()) setText(urls.first().toLocalFile()); }
    };

    lineEdit = new PathLineEdit(this);
    lineEdit->setPlaceholderText(QString::fromUtf8("请在此添加程序，可拖动程序到此处"));
    btnBrowse = new QPushButton(QString::fromUtf8("选择程序"), this);
    btnCancel = new QPushButton(QString::fromUtf8("取消"), this);
    btnAdd = new QPushButton(QString::fromUtf8("添加"), this);
    btnCancel->setEnabled(false);
    btnAdd->setEnabled(false);
    tableWidget = new DesktopSelectTable(this);

    tableWidget->setColumnCount(9);
    tableWidget->setHorizontalHeaderLabels({ QString::fromUtf8("\u7a0b\u5e8f"), QString::fromUtf8("\u8fd0\u884c\u72b6\u6001"), QString::fromUtf8("\u6301\u7eed\u8fd0\u884c\u65f6\u957f"), QString::fromUtf8("\u4e0a\u6b21\u91cd\u542f"), QString::fromUtf8("\u5b88\u62a4\u6b21\u6570"), QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u89c4\u5219"), QString::fromUtf8("\u4e0b\u6b21\u91cd\u542f"), QString::fromUtf8("\u542f\u52a8\u5ef6\u65f6"), QString::fromUtf8("\u64cd\u4f5c") });
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
        hdr->setToolTip(QString::fromUtf8("\u7a0b\u5e8f\u91cd\u542f\u65f6\u7684\u542f\u52a8\u5ef6\u65f6\uff0c\u5355\u4f4d\u4e3a\u79d2\u3002\n\u9ed8\u8ba4 1 \u79d2\uff0c\u4e0d\u53ef\u4f4e\u4e8e\u6b64\u503c\u3002\n\u5b88\u62a4\u91cd\u542f\u3001\u5b9a\u65f6\u91cd\u542f\u5747\u4f7f\u7528\u6b64\u5ef6\u65f6\u3002\n\u5b9a\u65f6\u8fd0\u884c\u4e0d\u4f7f\u7528\u6b64\u9879\u3002"));
    }
    tableWidget->verticalHeader()->setSectionsClickable(false);
    tableWidget->verticalHeader()->setHighlightSections(false);
    tableWidget->verticalHeader()->setFocusPolicy(Qt::NoFocus);
    tableWidget->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    tableWidget->verticalHeader()->hide();
    tableWidget->setMouseTracking(true);

    QWidget* top = new QWidget(this);
    QHBoxLayout* topLayout = new QHBoxLayout(top);
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
    viewMenu->addAction(QString::fromUtf8("操作日志"), this, []() {
        QString path = operationLogFilePath();
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

    QMenu* configMenu = menuBar()->addMenu(QString::fromUtf8("\u914d\u7f6e"));
    configMenu->addAction(QString::fromUtf8("\u5bfc\u5165"), this, &SuperGuardian::importConfig);
    configMenu->addAction(QString::fromUtf8("\u5bfc\u51fa"), this, &SuperGuardian::exportConfig);
    configMenu->addAction(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192\u914d\u7f6e"), this, &SuperGuardian::showSmtpConfigDialog);
    configMenu->addSeparator();
    configMenu->addAction(QString::fromUtf8("\u91cd\u7f6e\u5168\u90e8\u914d\u7f6e"), this, &SuperGuardian::resetConfig);

    QMenu* operationMenu = menuBar()->addMenu(QString::fromUtf8("操作"));
    operationMenu->addAction(QString::fromUtf8("清空列表"), this, &SuperGuardian::clearListWithConfirmation);
    operationMenu->addAction(QString::fromUtf8("重置列宽"), this, &SuperGuardian::resetColumnWidths);

    QMenu* testMenu = menuBar()->addMenu(QString::fromUtf8("测试"));
    testMenu->addAction(QString::fromUtf8("测试自我守护"), this, &SuperGuardian::runSelfGuardTest);

    // ---- 主题切换按钮（菜单栏右侧角落） ----
    themeToggleBtn = new QToolButton(this);
    themeToggleBtn->setAutoRaise(true);
    themeToggleBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    themeToggleBtn->setCursor(Qt::PointingHandCursor);
    themeToggleBtn->setIconSize(QSize(18, 18));
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
    connect(btnAdd, &QPushButton::clicked, this, [this]() { QString p=lineEdit->text().trimmed(); if(!p.isEmpty()) { addProgram(p); lineEdit->clear(); } });

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SuperGuardian::checkProcesses);
    timer->start(2000);

    connect(tableWidget, &QTableWidget::cellDoubleClicked, this, &SuperGuardian::onTableDoubleClicked);
    connect(tray, &QSystemTrayIcon::activated, this, &SuperGuardian::onTrayActivated);
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section == 8) return;

        QHeaderView* header = tableWidget->horizontalHeader();
        if (header->sortIndicatorSection() == section && sortState == 2) {
            sortState = 0;
            header->setSortIndicatorShown(false);
            tableWidget->sortItems(0, Qt::AscendingOrder);
        } else if (header->sortIndicatorSection() == section && sortState == 1) {
            sortState = 2;
            header->setSortIndicatorShown(true);
            header->setSortIndicator(section, Qt::DescendingOrder);
            tableWidget->sortItems(section, Qt::DescendingOrder);
        } else {
            sortState = 1;
            header->setSortIndicatorShown(true);
            header->setSortIndicator(section, Qt::AscendingOrder);
            tableWidget->sortItems(section, Qt::AscendingOrder);
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

void SuperGuardian::toggleTheme() {
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    QString current = s.value("theme", "light").toString();
    QString next = (current == "dark") ? "light" : "dark";
    s.setValue("theme", next);
    applyTheme(next);
}
