#pragma once

#include <QtWidgets>
#include "GuardItem.h"
#include "EmailService.h"

class SuperGuardian : public QMainWindow
{
    Q_OBJECT

public:
    SuperGuardian(QWidget *parent = nullptr);
    ~SuperGuardian();

private:
    QLineEdit* lineEdit;
    QPushButton* btnBrowse;
    QPushButton* btnCancel;
    QPushButton* btnAdd;
    QTableWidget* tableWidget;

    QList<GuardItem> items;
    QSystemTrayIcon* tray;
    QMenu* trayMenu;
    QAction* selfGuardAct;
    QAction* autostartAct;
    QAction* emailEnabledAct = nullptr;
    QAction* trayEmailAct = nullptr;
    QAction* minimizeToTrayAct = nullptr;

    // 全局功能 QAction（功能菜单）
    QAction* globalGuardAct = nullptr;
    QAction* globalRestartAct = nullptr;
    QAction* globalRunAct = nullptr;

    // 工具栏图标按钮
    QToolButton* selfGuardBtn = nullptr;
    QToolButton* autostartBtn = nullptr;
    QToolButton* minimizeToTrayBtn = nullptr;
    QToolButton* globalGuardBtn = nullptr;
    QToolButton* globalRestartBtn = nullptr;
    QToolButton* globalRunBtn = nullptr;
    QToolButton* globalEmailBtn = nullptr;
    QTimer* timer;
    SmtpConfig smtpConfig;

    QToolButton* themeToggleBtn = nullptr;
    QToolButton* pinToggleBtn = nullptr;
    int sortState = 0;
    int activeSortSection = -1;
    bool autoResizingColumns = false;
    bool pendingResetColumnWidths = false;
    bool m_revertingHeader = false;
    bool m_exiting = false;
    QDateTime m_lastSaveTime;

    QStringList duplicateWhitelist;
    QList<ScheduleRule> copiedScheduleRules;
    QDateTime copiedRulesTime;
    GuardItem copiedItem;
    bool hasCopiedItem = false;

    void addProgram(const QString& path, const QString& extraArgs = QString());
    void parseAndAddFromInput();
    void setupTableRow(int row, const GuardItem& item);
    void updateButtonStates(int row);
    void loadSettings();
    void saveSettings();
    void applySavedTrayOptions();
    void startWatchdogHelper();
    void stopWatchdogHelper();
    void applyTheme(const QString& theme);
    void syncSelfGuardListEntry(bool enabled);
    void runSelfGuardTest();
    int findItemIndexById(const QString& id) const;
    int findRowById(const QString& id) const;
    QString rowId(int row) const;
    void clearListWithConfirmation();
    void closeAllOperations();
    void distributeColumnWidths();
    void saveColumnWidths();
    void resetColumnWidths();
    void toggleTheme();
    void exportConfig();
    void importConfig();
    void resetConfig();
    void rebuildTableFromItems();
    void trySendNotification(GuardItem& item, const QString& event, const QString& detail);
    void handleRowMoved(int fromRow, int toRow);
    void handleRowsMoved(const QList<int>& rows, int insertBefore);
    void closeAllGuards();
    void closeAllScheduledRestart();
    void closeAllScheduledRun();
    QString formatStartDelay(int secs) const;
    QString formatDuration(qint64 secs) const;
    void createDesktopShortcut();
    void showUpdateDialog();
    void checkForOnlineUpdates(bool automatic, QWidget* uiParent = nullptr, QLabel* statusLabel = nullptr);
    bool installUpdatePackage(const QString& selectedFile, QWidget* dialogParent, bool confirmSelection,
        const QString& confirmMessage = QString());
    void centerWindow();
    void showDuplicateWhitelistDialog();
    void toggleAlwaysOnTop();
    void testDuplicateAdd();
    void showOperationLog();
    void showRuntimeLog();
    void showGuardLog();
    void showScheduledRestartLog();
    void showScheduledRunLog();
    void performSort();
    void saveSortState();
    void saveColumnVisibility();
    void restoreColumnVisibility();
    void onHeaderContextMenu(const QPoint& pos);
    void saveHeaderOrder();
    void restoreHeaderOrder();
    void resetHeaderDisplay();
    void showAboutDialog();
    void exportDiagnosticInfo();
    void initSignals();
    void requestResetColumnWidths();
    void updateToolbarIcons();
    QIcon makeToolbarIcon(const QString& letter, bool active, const QString& theme) const;

private slots:
    void toggleVisible();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onSelfGuardToggled(bool on);
    void onAutostartToggled(bool on);
    void onExit();
    void onTableDoubleClicked(int row, int col);
    void checkProcesses();
    void onTableContextMenuRequested(const QPoint& pos);
    void contextStartProgram(int row);
    void contextKillProgram(int row);
    void contextToggleGuard(int row);
    void contextRemoveItem(int row);
    void contextSetScheduleRules(const QList<int>& rows, bool forRun, bool activateOnConfirm = false);
    void contextSetStartDelay(const QList<int>& rows);
    void contextSetRetryConfig(const QList<int>& rows);
    void contextSetEmailNotify(const QList<int>& rows);
    void contextSetLaunchArgs(const QList<int>& rows);
    void contextSetNote(const QList<int>& rows);
    void contextOpenFileLocation(int row);
    void showSmtpConfigDialog();
    void contextTogglePin(const QList<int>& rows);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
};

