#pragma once

#include <QtWidgets>
#include "GuardItem.h"

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
    QTimer* timer;

    QToolButton* themeToggleBtn = nullptr;
    int sortState = 0; // 0=default, 1=ascending, 2=descending
    bool autoResizingColumns = false;

    void addProgram(const QString& path);
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
    void contextSetScheduledRestart(const QList<int>& rows);
    void contextSetGuardDelay(const QList<int>& rows);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
};

