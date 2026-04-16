#include "ThemeManager.h"
#include <windows.h>
#include <QApplication>

using namespace Qt::Literals::StringLiterals;

static QString s_currentTheme = u"dark"_s;

QString currentThemeName() { return s_currentTheme; }

QString detectSystemThemeName() {
    HKEY hKey;
    DWORD useLight = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD data = 0;
        DWORD size = sizeof(data);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&data), &size) == ERROR_SUCCESS) {
            useLight = data;
        }
        RegCloseKey(hKey);
    }
    return useLight ? u"light"_s : u"dark"_s;
}

void applyAppTheme(const QString& theme) {
    s_currentTheme = theme;
    qApp->setStyleSheet(theme == "dark" ? darkStyleSheet() : lightStyleSheet());
}

QString darkStyleSheet() {
    return
        /* ===== WinUI 3 Fluent Design — Dark ===== */

        /* Base */
        "QWidget { background-color: #202020; color: #ffffff; }"
        "QLabel { background: transparent; }"

        /* Line Edit */
        "QLineEdit { background-color: #2d2d2d; color: #ffffff; "
            "border: 1px solid #555; border-bottom: 2px solid #888; "
            "border-radius: 4px; padding: 4px 10px; "
            "selection-background-color: #0078d4; selection-color: #ffffff; }"
        "QLineEdit:focus { border-bottom: 2px solid #60cdff; }"
        "QLineEdit:hover:!focus { border-color: #666; border-bottom-color: #999; }"

        /* Buttons */
        "QPushButton { background-color: #323232; color: #ffffff; "
            "border: 1px solid #555; border-bottom: 1px solid #444; "
            "border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover:!disabled { background-color: #3a3a3a; border-color: #666; }"
        "QPushButton:pressed { background-color: #2a2a2a; color: #bbbbbb; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; "
            "border-color: #404040; border-bottom-color: #404040; }"

        /* Accent Button */
        "#primaryBtn { background-color: #60cdff; color: #003046; "
            "border: 1px solid #4cc2ff; border-bottom: 1px solid #39b8ff; "
            "border-radius: 4px; }"
        "#primaryBtn:hover:!disabled { background-color: #77d5ff; border-color: #60cdff; }"
        "#primaryBtn:pressed { background-color: #4cc2ff; color: #004060; }"
        "#primaryBtn:disabled { background-color: #2a2a2a; color: #555; "
            "border-color: #404040; border-bottom-color: #404040; }"

        /* Tool Button */
        "QToolButton { background-color: #323232; color: #ffffff; "
            "border: 1px solid #555; border-radius: 4px; padding: 2px 8px; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }"
        "QToolButton:hover { background-color: #3a3a3a; }"
        "QToolButton:pressed { background-color: #2a2a2a; }"

        /* Menu Bar */
        "QMenuBar { background-color: #202020; border-bottom: 1px solid #333; "
            "spacing: 1px; padding: 2px 0px; }"
        "QMenuBar::item { padding: 4px 10px; border-radius: 4px; background: transparent; }"
        "QMenuBar::item:selected { background-color: #333; }"
        "QMenuBar::item:pressed { background-color: #383838; }"

        /* Menus */
        "QMenu { background-color: #2c2c2c; color: #ffffff; "
            "border: 1px solid #404040; border-radius: 6px; padding: 3px; }"
        "QMenu::item { padding: 7px 28px 7px 12px; border-radius: 4px; margin: 1px 3px; }"
        "QMenu::item:selected { background-color: #383838; }"
        "QMenu::item:disabled { color: #666; }"
        "QMenu::separator { height: 1px; background-color: #404040; margin: 3px 8px; }"
        "QMenu::indicator { width: 14px; height: 14px; border: none; margin-left: 6px; }"
        "QMenu::indicator:checked { image: url(:/SuperGuardian/light/check.png); }"

        /* Table */
        "QTableWidget, QTableView { background-color: #2d2d2d; "
            "border: 1px solid #3d3d3d; border-radius: 4px; "
            "gridline-color: #383838; outline: none; }"
        "QTableWidget::item:selected, QTableView::item:selected, QListView::item:selected, QTreeView::item:selected { "
            "background-color: #21466f; color: #ffffff; border-bottom: 2px solid #60cdff; }"
        "QHeaderView::section { background-color: #323232; color: #9d9d9d; "
            "font-weight: bold; border: none; "
            "border-right: 1px solid #3d3d3d; border-bottom: 1px solid #3d3d3d; padding: 6px; }"
        "QHeaderView::section:hover { background-color: #3a3a3a; color: #ffffff; }"

        /* Scrollbars */
        "QScrollBar:vertical { background: transparent; width: 12px; }"
        "QScrollBar::handle:vertical { background-color: #555; border-radius: 3px; "
            "min-height: 30px; margin: 2px 3px; }"
        "QScrollBar::handle:vertical:hover { background-color: #777; margin: 2px 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { background: transparent; height: 12px; }"
        "QScrollBar::handle:horizontal { background-color: #555; border-radius: 3px; "
            "min-width: 30px; margin: 3px 2px; }"
        "QScrollBar::handle:horizontal:hover { background-color: #777; margin: 2px 2px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"

        /* ComboBox */
        "QComboBox { background-color: #2d2d2d; color: #ffffff; "
            "border: 1px solid #555; border-radius: 4px; padding: 4px 8px; }"
        "QComboBox:hover { border-color: #666; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox QAbstractItemView { background-color: #2c2c2c; "
            "border: 1px solid #404040; "
            "selection-background-color: #383838; selection-color: #ffffff; "
            "outline: none; padding: 2px; }"

        /* SpinBox / TimeEdit */
        "QSpinBox, QTimeEdit, QDoubleSpinBox { background-color: #2d2d2d; color: #ffffff; "
            "border: 1px solid #555; border-bottom: 2px solid #888; "
            "border-radius: 4px; padding: 4px 8px; }"
        "QSpinBox:focus, QTimeEdit:focus, QDoubleSpinBox:focus { border-bottom: 2px solid #60cdff; }"
        "QSpinBox::up-button, QSpinBox::down-button, "
            "QTimeEdit::up-button, QTimeEdit::down-button, "
            "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button "
            "{ width: 20px; border: none; border-radius: 2px; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover, "
            "QTimeEdit::up-button:hover, QTimeEdit::down-button:hover "
            "{ background-color: #3a3a3a; }"

        /* CheckBox */
        "QCheckBox { spacing: 6px; background: transparent; }"
        "QCheckBox::indicator { width: 15px; height: 15px; "
            "border: 2px solid #9d9d9d; border-radius: 4px; background-color: transparent; }"
        "QCheckBox::indicator:hover { border-color: #bbb; }"
        "QCheckBox::indicator:checked { background-color: #60cdff; border-color: #60cdff; "
            "image: url(:/SuperGuardian/dark/check.png); }"

        /* ListWidget */
        "QListWidget { background-color: #2d2d2d; color: #ffffff; "
            "border: 1px solid #3d3d3d; border-radius: 4px; outline: none; }"
        "QListWidget::item { padding: 5px 8px; border-radius: 3px; margin: 1px 2px; }"
        "QListWidget::item:selected { background-color: #005fb8; color: #ffffff; }"
        "QListWidget::item:hover:!selected { background-color: #383838; }"

        /* Dialog */
        "QDialog { background-color: #2d2d2d; }"

        /* GroupBox */
        "QGroupBox { font-weight: bold; border: 1px solid #404040; "
            "border-radius: 6px; margin-top: 8px; padding-top: 14px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px; }"

        /* Tooltip */
        "QToolTip { background-color: #2c2c2c; color: #ffffff; "
            "border: 1px solid #555; border-radius: 4px; padding: 6px 8px; }"

        /* Theme Toggle */
        "#themeToggleBtn { background: transparent; border: 1px solid #555; "
            "border-radius: 6px; padding: 0px; margin: 3px; }"
        "#themeToggleBtn:hover { background-color: #3a3a3a; }"
        "#themeToggleBtn:pressed { background-color: #333; }"

        /* Pin Toggle */
        "#pinToggleBtn { background: transparent; border: 1px solid #555; "
            "border-radius: 6px; padding: 0px; margin: 3px; }"
        "#pinToggleBtn:hover { background-color: #3a3a3a; }"
        "#pinToggleBtn:pressed { background-color: #333; }"
        "#pinToggleBtn:checked { background-color: #20476e; border-color: #20476e; }"

        /* Toolbar Icon Buttons */
        "#selfGuardBtn, #autostartBtn, #minimizeToTrayBtn, "
            "#globalGuardBtn, #globalRestartBtn, #globalRunBtn, #globalEmailBtn "
            "{ background: transparent; border: 1px solid #555; "
            "border-radius: 6px; padding: 0px; margin: 3px; }"
        "#selfGuardBtn:hover, #autostartBtn:hover, #minimizeToTrayBtn:hover, "
            "#globalGuardBtn:hover, #globalRestartBtn:hover, #globalRunBtn:hover, #globalEmailBtn:hover "
            "{ background-color: #3a3a3a; }"
        "#selfGuardBtn:pressed, #autostartBtn:pressed, #minimizeToTrayBtn:pressed, "
            "#globalGuardBtn:pressed, #globalRestartBtn:pressed, #globalRunBtn:pressed, #globalEmailBtn:pressed "
            "{ background-color: #333; }"
        "#selfGuardBtn:checked, #autostartBtn:checked, #minimizeToTrayBtn:checked, "
            "#globalGuardBtn:checked, #globalRestartBtn:checked, #globalRunBtn:checked, #globalEmailBtn:checked "
                    "{ background-color: #20476e; border-color: #20476e; }"
                ;
            }

            QString lightStyleSheet() {
                return
                    /* ===== WinUI 3 Fluent Design — Light ===== */

                    /* Base */
                    "QWidget { background-color: #f3f3f3; color: #1a1a1a; }"
                    "QLabel { background: transparent; }"

                    /* Line Edit */
                    "QLineEdit { background-color: #ffffff; color: #1a1a1a; "
                        "border: 1px solid #d1d1d1; border-bottom: 2px solid #9a9a9a; "
                        "border-radius: 4px; padding: 4px 10px; "
                        "selection-background-color: #005fb7; selection-color: #ffffff; }"
                    "QLineEdit:focus { border-bottom: 2px solid #005fb7; }"
                    "QLineEdit:hover:!focus { border-color: #b8b8b8; border-bottom-color: #868686; }"

                    /* Buttons */
                    "QPushButton { background-color: #fbfbfb; color: #1a1a1a; "
                        "border: 1px solid #d1d1d1; border-bottom: 1px solid #b8b8b8; "
                        "border-radius: 4px; padding: 4px 12px; }"
                    "QPushButton:hover:!disabled { background-color: #f0f0f0; border-color: #c0c0c0; }"
                    "QPushButton:pressed { background-color: #e8e8e8; "
                        "border-bottom-color: #d1d1d1; color: #5d5d5d; }"
                    "QPushButton:disabled { background-color: #f5f5f5; color: #b8b8b8; "
                        "border-color: #e0e0e0; border-bottom-color: #e0e0e0; }"

                    /* Accent Button */
                    "#primaryBtn { background-color: #005fb7; color: #ffffff; "
                        "border: 1px solid #0055a5; border-bottom: 1px solid #004a90; "
                        "border-radius: 4px; }"
                    "#primaryBtn:hover:!disabled { background-color: #0067c0; border-color: #005fb7; }"
                    "#primaryBtn:pressed { background-color: #004a90; color: #c0dcf0; }"
                    "#primaryBtn:disabled { background-color: #f5f5f5; color: #b8b8b8; "
                        "border-color: #e0e0e0; border-bottom-color: #e0e0e0; }"

                    /* Tool Button */
                    "QToolButton { background-color: #fbfbfb; color: #1a1a1a; "
                        "border: 1px solid #d1d1d1; border-radius: 4px; padding: 2px 8px; }"
                    "QToolButton::menu-indicator { image: none; width: 0px; }"
                    "QToolButton:hover { background-color: #f0f0f0; }"
                    "QToolButton:pressed { background-color: #e8e8e8; }"

                    /* Menu Bar */
                    "QMenuBar { background-color: #f3f3f3; border-bottom: 1px solid #e5e5e5; "
                        "spacing: 1px; padding: 2px 0px; }"
                    "QMenuBar::item { padding: 4px 10px; border-radius: 4px; background: transparent; }"
                    "QMenuBar::item:selected { background-color: #e8e8e8; }"
                    "QMenuBar::item:pressed { background-color: #e0e0e0; }"

                    /* Menus */
                    "QMenu { background-color: #f9f9f9; color: #1a1a1a; "
                        "border: 1px solid #e0e0e0; border-radius: 6px; padding: 3px; }"
                    "QMenu::item { padding: 7px 28px 7px 12px; border-radius: 4px; margin: 1px 3px; }"
                    "QMenu::item:selected { background-color: #ebebeb; }"
                    "QMenu::item:disabled { color: #b8b8b8; }"
                    "QMenu::separator { height: 1px; background-color: #e5e5e5; margin: 3px 8px; }"
                    "QMenu::indicator { width: 14px; height: 14px; border: none; margin-left: 6px; }"
                    "QMenu::indicator:checked { image: url(:/SuperGuardian/dark/check.png); }"

                    /* Table */
                    "QTableWidget, QTableView { background-color: #ffffff; "
                        "border: 1px solid #e0e0e0; border-radius: 4px; "
                        "gridline-color: #f0f0f0; outline: none; }"
                    "QTableWidget::item:selected, QTableView::item:selected, QListView::item:selected, QTreeView::item:selected { "
                        "background-color: #dbeafe; color: #1a1a1a; border-bottom: 2px solid #005fb7; }"
                    "QHeaderView::section { background-color: #f5f5f5; color: #616161; "
                        "font-weight: bold; border: none; "
                        "border-right: 1px solid #e5e5e5; border-bottom: 1px solid #e5e5e5; padding: 6px; }"
                    "QHeaderView::section:hover { background-color: #ebebeb; color: #1a1a1a; }"

                    /* Scrollbars */
                    "QScrollBar:vertical { background: transparent; width: 12px; }"
                    "QScrollBar::handle:vertical { background-color: #c0c0c0; border-radius: 3px; "
                        "min-height: 30px; margin: 2px 3px; }"
                    "QScrollBar::handle:vertical:hover { background-color: #a0a0a0; margin: 2px 2px; }"
                    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                    "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
                    "QScrollBar:horizontal { background: transparent; height: 12px; }"
                    "QScrollBar::handle:horizontal { background-color: #c0c0c0; border-radius: 3px; "
                        "min-width: 30px; margin: 3px 2px; }"
                    "QScrollBar::handle:horizontal:hover { background-color: #a0a0a0; margin: 2px 2px; }"
                    "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
                    "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"

                    /* ComboBox */
                    "QComboBox { background-color: #ffffff; color: #1a1a1a; "
                        "border: 1px solid #d1d1d1; border-radius: 4px; padding: 4px 8px; }"
                    "QComboBox:hover { border-color: #b8b8b8; }"
                    "QComboBox::drop-down { border: none; width: 24px; }"
                    "QComboBox QAbstractItemView { background-color: #f9f9f9; "
                        "border: 1px solid #e0e0e0; "
                        "selection-background-color: #ebebeb; selection-color: #1a1a1a; "
                        "outline: none; padding: 2px; }"

                    /* SpinBox / TimeEdit */
                    "QSpinBox, QTimeEdit, QDoubleSpinBox { background-color: #ffffff; color: #1a1a1a; "
                        "border: 1px solid #d1d1d1; border-bottom: 2px solid #9a9a9a; "
                        "border-radius: 4px; padding: 4px 8px; }"
                    "QSpinBox:focus, QTimeEdit:focus, QDoubleSpinBox:focus { border-bottom: 2px solid #005fb7; }"
                    "QSpinBox::up-button, QSpinBox::down-button, "
                        "QTimeEdit::up-button, QTimeEdit::down-button, "
                        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button "
                        "{ width: 20px; border: none; border-radius: 2px; }"
                    "QSpinBox::up-button:hover, QSpinBox::down-button:hover, "
                        "QTimeEdit::up-button:hover, QTimeEdit::down-button:hover "
                        "{ background-color: #ebebeb; }"

                    /* CheckBox */
                    "QCheckBox { spacing: 6px; background: transparent; }"
                    "QCheckBox::indicator { width: 15px; height: 15px; "
                        "border: 2px solid #8a8a8a; border-radius: 4px; background-color: #ffffff; }"
                    "QCheckBox::indicator:hover { border-color: #616161; }"
                    "QCheckBox::indicator:checked { background-color: #005fb7; border-color: #005fb7; "
                        "image: url(:/SuperGuardian/light/check.png); }"

                    /* ListWidget */
                    "QListWidget { background-color: #ffffff; "
                        "border: 1px solid #e0e0e0; border-radius: 4px; outline: none; }"
                    "QListWidget::item { padding: 5px 8px; border-radius: 3px; margin: 1px 2px; }"
                    "QListWidget::item:selected { background-color: #005fb8; color: #ffffff; }"
                    "QListWidget::item:hover:!selected { background-color: #f5f5f5; }"

                    /* Dialog */
                    "QDialog { background-color: #f3f3f3; }"

                    /* GroupBox */
                    "QGroupBox { font-weight: bold; border: 1px solid #e0e0e0; "
                        "border-radius: 6px; margin-top: 8px; padding-top: 14px; }"
                    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px; }"

                    /* Tooltip */
                    "QToolTip { background-color: #f9f9f9; color: #1a1a1a; "
                        "border: 1px solid #e0e0e0; border-radius: 4px; padding: 6px 8px; }"

                    /* Theme Toggle */
                    "#themeToggleBtn { background: transparent; border: 1px solid #d1d1d1; "
                        "border-radius: 6px; padding: 0px; margin: 3px; }"
                    "#themeToggleBtn:hover { background-color: #e8e8e8; }"
                    "#themeToggleBtn:pressed { background-color: #e0e0e0; }"

                    /* Pin Toggle */
                    "#pinToggleBtn { background: transparent; border: 1px solid #d1d1d1; "
                        "border-radius: 6px; padding: 0px; margin: 3px; }"
                    "#pinToggleBtn:hover { background-color: #e8e8e8; }"
                    "#pinToggleBtn:pressed { background-color: #e0e0e0; }"
                    "#pinToggleBtn:checked { background-color: #ddedfe; border-color: #ddedfe; }"

                    /* Toolbar Icon Buttons */
                    "#selfGuardBtn, #autostartBtn, #minimizeToTrayBtn, "
                        "#globalGuardBtn, #globalRestartBtn, #globalRunBtn, #globalEmailBtn "
                        "{ background: transparent; border: 1px solid #d1d1d1; "
                        "border-radius: 6px; padding: 0px; margin: 3px; }"
                    "#selfGuardBtn:hover, #autostartBtn:hover, #minimizeToTrayBtn:hover, "
                        "#globalGuardBtn:hover, #globalRestartBtn:hover, #globalRunBtn:hover, #globalEmailBtn:hover "
                        "{ background-color: #e8e8e8; }"
                    "#selfGuardBtn:pressed, #autostartBtn:pressed, #minimizeToTrayBtn:pressed, "
                        "#globalGuardBtn:pressed, #globalRestartBtn:pressed, #globalRunBtn:pressed, #globalEmailBtn:pressed "
                        "{ background-color: #e0e0e0; }"
                    "#selfGuardBtn:checked, #autostartBtn:checked, #minimizeToTrayBtn:checked, "
                        "#globalGuardBtn:checked, #globalRestartBtn:checked, #globalRunBtn:checked, #globalEmailBtn:checked "
                        "{ background-color: #ddedfe; border-color: #ddedfe; }"
                ;
            }
