#include "ThemeManager.h"
#include <windows.h>
#include <QApplication>

static QString s_currentTheme = QStringLiteral("light");

QString currentThemeName() { return s_currentTheme; }

QString detectSystemThemeName() {
    HKEY hKey;
    DWORD useLight = 1;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD data = 1;
        DWORD size = sizeof(data);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&data), &size) == ERROR_SUCCESS) {
            useLight = data;
        }
        RegCloseKey(hKey);
    }
    return useLight ? QStringLiteral("light") : QStringLiteral("dark");
}

void applyAppTheme(const QString& theme) {
    s_currentTheme = theme;
    if (theme == "dark") {
        qApp->setStyleSheet(
            "QWidget { background-color: #222; color: #ddd; }"
            "QLineEdit, QHeaderView::section, QToolButton { border: 1px solid #555; }"
            "QLineEdit { border-radius: 4px; }"
            "QTableWidget { border: 1px solid #555; border-radius: 6px; }"
            "QPushButton { background-color: #3a3a3a; color: #fff; border: 1px solid #666; border-radius: 4px; padding: 4px 10px; }"
            "QToolButton { background-color: #3a3a3a; color: #fff; border-radius: 4px; padding: 2px 8px; }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"
            "QPushButton:hover:!disabled { background-color: #4a4a4a; }"
            "QToolButton:hover { background-color: #4a4a4a; }"
            "QPushButton:pressed { background-color: #2f2f2f; }"
            "QToolButton:pressed { background-color: #2f2f2f; }"
            "QPushButton:disabled { background-color: #2a2a2a; color: #777; border: 1px solid #444; }"
            "QMenu { background-color: #2c2c2c; color: #ddd; border: 1px solid #555; }"
            "QMenu::item:selected { background-color: #3d3d3d; }"
            "QMenu::separator { background-color: #666; height: 1px; margin: 2px 6px; }"
            "#themeToggleBtn { background: transparent; border: 1px solid #555; border-radius: 4px; padding: 0px; }"
            "#themeToggleBtn:hover { background-color: #4a4a4a; }"
            "#themeToggleBtn:pressed { background-color: #2f2f2f; }"
        );
    } else {
        qApp->setStyleSheet(
            "QWidget { background-color: #ffffff; color: #000000; }"
            "QLineEdit, QHeaderView::section, QToolButton { background-color: #ffffff; color: #000000; border: 1px solid #9a9a9a; }"
            "QHeaderView::section { background-color: #eaeaea; }"
            "QLineEdit { border-radius: 4px; }"
            "QTableWidget { border: 1px solid #9a9a9a; border-radius: 6px; }"
            "QPushButton { background-color: #f5f5f5; color: #111; border: 1px solid #7d7d7d; border-radius: 4px; padding: 4px 10px; }"
            "QToolButton { background-color: #f5f5f5; color: #111; border: 1px solid #7d7d7d; border-radius: 4px; }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"
            "QPushButton:hover:!disabled { background-color: #ededed; border-color: #666; }"
            "QToolButton:hover { background-color: #ededed; border-color: #666; }"
            "QPushButton:pressed { background-color: #e3e3e3; }"
            "QToolButton:pressed { background-color: #e3e3e3; }"
            "QPushButton:disabled { background-color: #dddddd; color: #888; border: 1px solid #c4c4c4; }"
            "QTableView { gridline-color: #b5b5b5; }"
            "QMenu { background-color: #f0f0f0; color: #000; border: 1px solid #999; }"
            "QMenu::item:selected { background-color: #d8d8d8; }"
            "QMenu::separator { background-color: #999; height: 1px; margin: 2px 6px; }"
            "#themeToggleBtn { background: transparent; border: 1px solid #9a9a9a; border-radius: 4px; padding: 0px; }"
            "#themeToggleBtn:hover { background-color: #ededed; }"
            "#themeToggleBtn:pressed { background-color: #e3e3e3; }"
        );
    }
}
