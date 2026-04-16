#include "SuperGuardian.h"
#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include "DialogHelpers.h"
#include "LogDatabase.h"
#include "ConfigDatabase.h"
#include <QtWidgets>
#include <QDesktopServices>
#include <QUrl>
#include <functional>
#include <memory>
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
        std::function<void(const QStringList&)> onBatchDrop;
        PathLineEdit(QWidget* p=nullptr) : QLineEdit(p) { setAcceptDrops(true); }
    protected:
        void dragEnterEvent(QDragEnterEvent* e) override { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
        void dropEvent(QDropEvent* e) override {
            const QList<QUrl> urls = e->mimeData()->urls();
            if (urls.isEmpty()) return;
            if (urls.size() == 1) {
                setText(QDir::toNativeSeparators(urls.first().toLocalFile()));
            } else {
                QStringList paths;
                for (const QUrl& u : urls) paths << QDir::toNativeSeparators(u.toLocalFile());
                if (onBatchDrop) onBatchDrop(paths);
            }
        }
    };

    lineEdit = new PathLineEdit(this);
    static_cast<PathLineEdit*>(lineEdit)->onBatchDrop = [this](const QStringList& paths) {
        QStringList names;
        for (const QString& f : paths) names << QFileInfo(f).fileName();
        QString msg = u"确认添加以下 %1 个程序？\n\n%2"_s.arg(paths.size()).arg(names.join(u"\n"_s));
        if (showMessageDialog(this, u"批量添加程序"_s, msg, true)) {
            for (const QString& f : paths) addProgram(f);
        }
    };
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

    tableWidget->setColumnCount(11);
    tableWidget->setHorizontalHeaderLabels({ u"UUID"_s, u"程序"_s, u"运行状态"_s, u"持续运行时长"_s, u"上次执行"_s, u"被守护次数"_s, u"持续守护时长"_s, u"定时规则"_s, u"下次执行"_s, u"启动延时"_s, u"操作"_s });
    tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    tableWidget->horizontalHeader()->setSectionResizeMode(10, QHeaderView::Fixed);
    tableWidget->setColumnWidth(10, 300);
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
    if (QTableWidgetItem* hdr = tableWidget->horizontalHeaderItem(9)) {
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
    trayEmailAct = trayMenu->addAction(u"全局邮件提醒"_s);
    trayEmailAct->setCheckable(true);
    minimizeToTrayAct = trayMenu->addAction(u"启动时最小化到托盘"_s);
    minimizeToTrayAct->setCheckable(true);
    trayMenu->addSeparator();
    QAction* exitAct = trayMenu->addAction(u"退出"_s, this, &SuperGuardian::onExit);
    tray->setContextMenu(trayMenu);
    tray->show();

    // ---- 菜单栏：查看 - 选项 - 功能 - 配置 - 操作 - 帮助 ----
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
    optionsMenu->addAction(minimizeToTrayAct);

    QMenu* featureMenu = menuBar()->addMenu(u"功能"_s);
    globalGuardAct = featureMenu->addAction(u"全局守护"_s);
    globalGuardAct->setCheckable(true);
    globalGuardAct->setChecked(false);
    globalRestartAct = featureMenu->addAction(u"全局定时重启"_s);
    globalRestartAct->setCheckable(true);
    globalRestartAct->setChecked(false);
    globalRunAct = featureMenu->addAction(u"全局定时运行"_s);
    globalRunAct->setCheckable(true);
    globalRunAct->setChecked(false);
    emailEnabledAct = featureMenu->addAction(u"全局邮件提醒"_s);
    emailEnabledAct->setCheckable(true);
    emailEnabledAct->setChecked(false);

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
    operationMenu->addAction(u"关闭所有操作项"_s, this, &SuperGuardian::closeAllOperations);
    operationMenu->addSeparator();
    operationMenu->addAction(u"移动软件窗口到居中位置"_s, this, &SuperGuardian::centerWindow);
    operationMenu->addAction(u"添加桌面快捷方式"_s, this, &SuperGuardian::createDesktopShortcut);

    QMenu* helpMenu = menuBar()->addMenu(u"帮助"_s);
    helpMenu->addAction(u"更新"_s, this, &SuperGuardian::showUpdateDialog);
    helpMenu->addAction(u"导出诊断信息"_s, this, &SuperGuardian::exportDiagnosticInfo);
    helpMenu->addSeparator();
    helpMenu->addAction(u"测试自我守护"_s, this, &SuperGuardian::runSelfGuardTest);
    helpMenu->addAction(u"测试程序是否允许重复添加"_s, this, &SuperGuardian::testDuplicateAdd);
    helpMenu->addSeparator();
    helpMenu->addAction(u"关于 超级守护"_s, this, &SuperGuardian::showAboutDialog);

    // ---- 工具栏图标按钮（菜单栏右侧角落） ----
    auto makeTbBtn = [this](const QString& objName, const QString& tip) {
        QToolButton* btn = new QToolButton(this);
        btn->setObjectName(objName);
        btn->setAutoRaise(true);
        btn->setCheckable(true);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIconSize(QSize(20, 20));
        btn->setFixedSize(28, 28);
        btn->setToolTip(tip);
        return btn;
    };
    auto makeSep = [this]() {
        QFrame* sep = new QFrame(this);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setFixedWidth(28);
        sep->setFixedHeight(20);
        return sep;
    };

    // 选项组按钮
    selfGuardBtn = makeTbBtn(u"selfGuardBtn"_s, u"自我守护"_s);
    autostartBtn = makeTbBtn(u"autostartBtn"_s, u"开机自启"_s);
    minimizeToTrayBtn = makeTbBtn(u"minimizeToTrayBtn"_s, u"启动时最小化到托盘"_s);

    // 功能组按钮
    globalGuardBtn = makeTbBtn(u"globalGuardBtn"_s, u"全局守护"_s);
    globalRestartBtn = makeTbBtn(u"globalRestartBtn"_s, u"全局定时重启"_s);
    globalRunBtn = makeTbBtn(u"globalRunBtn"_s, u"全局定时运行"_s);
    globalEmailBtn = makeTbBtn(u"globalEmailBtn"_s, u"全局邮件提醒"_s);

    // 置顶 + 主题
    pinToggleBtn = makeTbBtn(u"pinToggleBtn"_s, u"置顶窗口"_s);
    connect(pinToggleBtn, &QToolButton::clicked, this, &SuperGuardian::toggleAlwaysOnTop);

    themeToggleBtn = makeTbBtn(u"themeToggleBtn"_s, u"切换深浅色"_s);
    themeToggleBtn->setCheckable(false);
    connect(themeToggleBtn, &QToolButton::clicked, this, &SuperGuardian::toggleTheme);

    QWidget* cornerWidget = new QWidget(this);
    QHBoxLayout* cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(3);
    // 选项组
    cornerLayout->addWidget(selfGuardBtn);
    cornerLayout->addWidget(autostartBtn);
    cornerLayout->addWidget(minimizeToTrayBtn);
    cornerLayout->addWidget(makeSep());
    // 功能组
    cornerLayout->addWidget(globalGuardBtn);
    cornerLayout->addWidget(globalRestartBtn);
    cornerLayout->addWidget(globalRunBtn);
    cornerLayout->addWidget(globalEmailBtn);
    cornerLayout->addWidget(makeSep());
    // 置顶 + 主题
    cornerLayout->addWidget(pinToggleBtn);
    cornerLayout->addWidget(themeToggleBtn);
    menuBar()->setCornerWidget(cornerWidget, Qt::TopRightCorner);

    initSignals();
}

SuperGuardian::~SuperGuardian()
{
}

// ---- 信号连接与初始化 ----

void SuperGuardian::initSignals() {
    connect(selfGuardAct, &QAction::toggled, this, &SuperGuardian::onSelfGuardToggled);
    connect(autostartAct, &QAction::toggled, this, &SuperGuardian::onAutostartToggled);

    // 同步托盘邮件提醒与菜单栏邮件提醒
    connect(emailEnabledAct, &QAction::toggled, this, [this](bool on) {
        trayEmailAct->blockSignals(true);
        trayEmailAct->setChecked(on);
        trayEmailAct->blockSignals(false);
        ConfigDatabase::instance().setValue(u"emailEnabled"_s, on);
        updateToolbarIcons();
    });
    connect(trayEmailAct, &QAction::toggled, this, [this](bool on) {
        emailEnabledAct->blockSignals(true);
        emailEnabledAct->setChecked(on);
        emailEnabledAct->blockSignals(false);
        ConfigDatabase::instance().setValue(u"emailEnabled"_s, on);
        updateToolbarIcons();
    });

    connect(minimizeToTrayAct, &QAction::toggled, this, [this](bool on) {
        ConfigDatabase::instance().setValue(u"minimizeToTray"_s, on);
        updateToolbarIcons();
    });

    // 全局功能菜单项信号
    connect(globalGuardAct, &QAction::toggled, this, [this](bool on) {
        ConfigDatabase::instance().setValue(u"globalGuardEnabled"_s, on);
        updateToolbarIcons();
    });
    connect(globalRestartAct, &QAction::toggled, this, [this](bool on) {
        ConfigDatabase::instance().setValue(u"globalRestartEnabled"_s, on);
        updateToolbarIcons();
    });
    connect(globalRunAct, &QAction::toggled, this, [this](bool on) {
        ConfigDatabase::instance().setValue(u"globalRunEnabled"_s, on);
        updateToolbarIcons();
    });

    // 工具栏按钮 ↔ 菜单项双向同步（选项组）
    connect(selfGuardBtn, &QToolButton::clicked, this, [this]() {
        if (selfGuardAct) selfGuardAct->setChecked(!selfGuardAct->isChecked());
    });
    connect(autostartBtn, &QToolButton::clicked, this, [this]() {
        if (autostartAct) autostartAct->setChecked(!autostartAct->isChecked());
    });
    connect(minimizeToTrayBtn, &QToolButton::clicked, this, [this]() {
        if (minimizeToTrayAct) minimizeToTrayAct->setChecked(!minimizeToTrayAct->isChecked());
    });

    // 工具栏按钮 ↔ 菜单项双向同步（功能组）
    connect(globalGuardBtn, &QToolButton::clicked, this, [this]() {
        if (globalGuardAct) globalGuardAct->setChecked(!globalGuardAct->isChecked());
    });
    connect(globalRestartBtn, &QToolButton::clicked, this, [this]() {
        if (globalRestartAct) globalRestartAct->setChecked(!globalRestartAct->isChecked());
    });
    connect(globalRunBtn, &QToolButton::clicked, this, [this]() {
        if (globalRunAct) globalRunAct->setChecked(!globalRunAct->isChecked());
    });
    connect(globalEmailBtn, &QToolButton::clicked, this, [this]() {
        if (emailEnabledAct) emailEnabledAct->setChecked(!emailEnabledAct->isChecked());
    });

    // selfGuardAct/autostartAct toggled 后也刷新图标
    connect(selfGuardAct, &QAction::toggled, this, [this]() { updateToolbarIcons(); });
    connect(autostartAct, &QAction::toggled, this, [this]() { updateToolbarIcons(); });

    connect(btnBrowse, &QPushButton::clicked, this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(this, u"选择程序"_s, "", "Executable (*.exe);;All Files (*)");
        if (files.isEmpty()) return;
        if (files.size() == 1) {
            lineEdit->setText(files.first());
        } else {
            QStringList names;
            for (const QString& f : files) names << QFileInfo(f).fileName();
            QString msg = u"确认添加以下 %1 个程序？\n\n%2"_s.arg(files.size()).arg(names.join(u"\n"_s));
            if (showMessageDialog(this, u"批量添加程序"_s, msg, true)) {
                for (const QString& f : files) addProgram(f);
            }
        }
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
    timer->start(200);

    if (tableWidget && tableWidget->model()) {
        connect(tableWidget->model(), &QAbstractItemModel::dataChanged,
            this, [this](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                requestResetColumnWidths();
            });
        connect(tableWidget->model(), &QAbstractItemModel::rowsInserted,
            this, [this](const QModelIndex&, int, int) {
                requestResetColumnWidths();
            });
        connect(tableWidget->model(), &QAbstractItemModel::rowsRemoved,
            this, [this](const QModelIndex&, int, int) {
                requestResetColumnWidths();
            });
        connect(tableWidget->model(), &QAbstractItemModel::modelReset,
            this, [this]() {
                requestResetColumnWidths();
            });
    }

    connect(tray, &QSystemTrayIcon::activated, this, &SuperGuardian::onTrayActivated);
    static_cast<DesktopSelectTable*>(tableWidget)->onCellDoubleClicked = [this](int row, int col) {
        if (col == 10) return;
        onTableDoubleClicked(row, col);
    };
    static_cast<DesktopSelectTable*>(tableWidget)->onRowMoved = [this](int from, int to) { handleRowMoved(from, to); };
    static_cast<DesktopSelectTable*>(tableWidget)->onRowsMoved = [this](const QList<int>& rows, int insertBefore) { handleRowsMoved(rows, insertBefore); };
    static_cast<DesktopSelectTable*>(tableWidget)->onKeyPressed = [this](int row, int key) {
        if (key == Qt::Key_F2) {
            contextSetNote(QList<int>{row});
        }
    };
    static_cast<DesktopSelectTable*>(tableWidget)->onDeletePressed = [this](const QList<int>& rows) {
        if (rows.isEmpty()) return;
        QString msg = rows.size() == 1
            ? u"确认移除此程序吗？"_s
            : u"确认移除选中的 %1 个程序吗？"_s.arg(rows.size());
        if (!showMessageDialog(this, u"移除"_s, msg, true)) return;
        for (int i = rows.size() - 1; i >= 0; --i) {
            int row = rows[i];
            int idx = findItemIndexById(rowId(row));
            if (idx >= 0) items.removeAt(idx);
            tableWidget->removeRow(row);
        }
        saveSettings();
    };

    // 表头排序（点击列标题循环：升序→降序→默认）
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section == 10) return;
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
    auto& cfg = ConfigDatabase::instance();
    QString theme = cfg.contains(u"theme"_s) ? cfg.value(u"theme"_s).toString() : detectSystemThemeName();
    if (!cfg.contains(u"theme"_s)) cfg.setValue(u"theme"_s, theme);
    applyTheme(theme);

    // 恢复排序状态
    activeSortSection = cfg.value(u"sortSection"_s, -1).toInt();
    sortState = cfg.value(u"sortState"_s, 0).toInt();
    if (sortState != 0 && activeSortSection >= 0 && activeSortSection < 10)
        performSort();

    restoreColumnVisibility();
    restoreHeaderOrder();

    // 恢复置顶状态
    if (cfg.value(u"alwaysOnTop"_s, false).toBool()) {
        QTimer::singleShot(0, this, [this]() {
            HWND hwnd = (HWND)winId();
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            if (pinToggleBtn) pinToggleBtn->setChecked(true);
        });
    }

    if (!cfg.contains(u"desktopShortcutPrompted"_s)) {
        QTimer::singleShot(0, this, [this]() {
            auto& db = ConfigDatabase::instance();
            if (db.contains(u"desktopShortcutPrompted"_s))
                return;

            const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
            const QString shortcutPath = QDir(desktop).filePath(u"超级守护.lnk"_s);
            if (QFileInfo::exists(shortcutPath)) {
                db.setValue(u"desktopShortcutPrompted"_s, true);
                return;
            }

            db.setValue(u"desktopShortcutPrompted"_s, true);
            if (showMessageDialog(this, u"创建桌面快捷方式"_s,
                u"是否为\"超级守护\"创建桌面快捷方式？\n此提示仅在首次运行时出现一次。"_s, true)) {
                createDesktopShortcut();
            }
        });
    }

    if (cfg.value(u"autoCheckUpdates"_s, false).toBool()) {
        auto delayedUpdateCheck = std::make_shared<std::function<void()>>();
        *delayedUpdateCheck = [this, delayedUpdateCheck]() {
            if (QApplication::activeModalWidget()) {
                QTimer::singleShot(1200, this, *delayedUpdateCheck);
                return;
            }
            checkForOnlineUpdates(true, this);
        };
        QTimer::singleShot(2500, this, *delayedUpdateCheck);
    }

    // 表头拖动排序：操作列始终在最右
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionMoved, this, [this](int, int, int) {
        if (m_revertingHeader) return;
        QHeaderView* header = tableWidget->horizontalHeader();
        int lastVisual = header->count() - 1;
        if (header->visualIndex(10) != lastVisual) {
            m_revertingHeader = true;
            header->moveSection(header->visualIndex(10), lastVisual);
            m_revertingHeader = false;
        }
        saveHeaderOrder();
    });

    // 列宽调整：保存列宽
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logicalIndex, int, int) {
        if (autoResizingColumns || logicalIndex == 10) return;
        saveColumnWidths();
    });
}
