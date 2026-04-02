#include "SuperGuardian.h"
#include "AppStorage.h"
#include "DialogHelpers.h"
#include "GuardTableWidgets.h"
#include "ThemeManager.h"
#include <QtWidgets>
#include <windows.h>

// ---- 信号连接与初始化 ----

void SuperGuardian::initSignals() {
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

    connect(tray, &QSystemTrayIcon::activated, this, &SuperGuardian::onTrayActivated);
    static_cast<DesktopSelectTable*>(tableWidget)->onCellDoubleClicked = [this](int row, int col) {
        if (col == 9) return;
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
            ? QString::fromUtf8("\u786e\u8ba4\u79fb\u9664\u6b64\u7a0b\u5e8f\u5417\uff1f")
            : QString::fromUtf8("\u786e\u8ba4\u79fb\u9664\u9009\u4e2d\u7684 %1 \u4e2a\u7a0b\u5e8f\u5417\uff1f").arg(rows.size());
        if (!showMessageDialog(this, QString::fromUtf8("\u79fb\u9664"), msg, true)) return;
        for (int i = rows.size() - 1; i >= 0; --i) {
            int row = rows[i];
            int idx = findItemIndexByPath(rowPath(row));
            if (idx >= 0) items.removeAt(idx);
            tableWidget->removeRow(row);
        }
        saveSettings();
    };

    // 表头排序（点击列标题循环：升序→降序→默认）
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section == 9) return;
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
    if (sortState != 0 && activeSortSection >= 0 && activeSortSection < 9)
        performSort();

    restoreColumnVisibility();
    restoreHeaderOrder();

    // 恢复置顶状态
    if (s.value("alwaysOnTop", false).toBool()) {
        QTimer::singleShot(0, this, [this]() {
            HWND hwnd = (HWND)winId();
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            if (pinToggleBtn) pinToggleBtn->setChecked(true);
        });
    }

    // 表头拖动排序：操作列始终在最右
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionMoved, this, [this](int, int, int) {
        if (m_revertingHeader) return;
        QHeaderView* header = tableWidget->horizontalHeader();
        int lastVisual = header->count() - 1;
        if (header->visualIndex(9) != lastVisual) {
            m_revertingHeader = true;
            header->moveSection(header->visualIndex(9), lastVisual);
            m_revertingHeader = false;
        }
        saveHeaderOrder();
    });

    // 列宽调整：保存列宽
    connect(tableWidget->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logicalIndex, int, int) {
        if (autoResizingColumns || logicalIndex == 9) return;
        saveColumnWidths();
    });
}
