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

    QVector<GuardItem> items;
    QSystemTrayIcon* tray;
    QMenu* trayMenu;
    QAction* selfGuardAct;
    QAction* autostartAct;
    QAction* emailEnabledAct = nullptr;
    QAction* trayEmailAct = nullptr;
    QTimer* timer;
    SmtpConfig smtpConfig;

    QToolButton* themeToggleBtn = nullptr;
    int sortState = 0;
    bool autoResizingColumns = false;

    void addProgram(const QString& path);
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
    int findItemIndexByPath(const QString& path) const;
    int findRowByPath(const QString& path) const;
    QString rowPath(int row) const;
    void clearListWithConfirmation();
    void distributeColumnWidths();
    void saveColumnWidths();
    void resetColumnWidths();
    void toggleTheme();
    void exportConfig();
    void importConfig();
    void resetConfig();
    void rebuildTableFromItems();
    void trySendNotification(GuardItem& item, const QString& event, const QString& detail);

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
    void contextSetScheduleRules(const QList<int>& rows, bool forRun);
    void contextSetStartDelay(const QList<int>& rows);
    void contextSetRetryConfig(const QList<int>& rows);
    void contextSetEmailNotify(const QList<int>& rows);
    void showSmtpConfigDialog();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
};

