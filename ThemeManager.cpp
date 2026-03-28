#include "ThemeManager.h"
#include <windows.h>
#include <QApplication>

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
    if (theme == "dark") {
        qApp->setStyleSheet(
            "QWidget { background-color: #222; color: #ddd; }"
            "QLineEdit, QTableWidget, QHeaderView::section, QToolButton { border: 1px solid #555; }"
            "QPushButton { background-color: #3a3a3a; color: #fff; border: 1px solid #666; border-radius: 4px; padding: 4px 10px; }"
            "QToolButton { background-color: #3a3a3a; color: #fff; border-radius: 4px; padding: 2px 8px; font-size: 18px; }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"
            "QPushButton:hover:!disabled { background-color: #4a4a4a; }"
            "QToolButton:hover { background-color: #4a4a4a; }"
            "QPushButton:pressed { background-color: #2f2f2f; }"
            "QToolButton:pressed { background-color: #2f2f2f; }"
            "QPushButton:disabled { background-color: #2a2a2a; color: #777; border: 1px solid #444; }"
        );
    } else {
        qApp->setStyleSheet(
            "QWidget { background-color: #ffffff; color: #000000; }"
            "QLineEdit, QTableWidget, QHeaderView::section, QToolButton { background-color: #ffffff; color: #000000; border: 1px solid #9a9a9a; }"
            "QPushButton { background-color: #f5f5f5; color: #111; border: 1px solid #7d7d7d; border-radius: 4px; padding: 4px 10px; }"
            "QToolButton { background-color: #f5f5f5; color: #111; border: 1px solid #7d7d7d; border-radius: 4px; padding: 2px 8px; font-size: 18px; }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"
            "QPushButton:hover:!disabled { background-color: #ededed; border-color: #666; }"
            "QToolButton:hover { background-color: #ededed; border-color: #666; }"
            "QPushButton:pressed { background-color: #e3e3e3; }"
            "QToolButton:pressed { background-color: #e3e3e3; }"
            "QPushButton:disabled { background-color: #dddddd; color: #888; border: 1px solid #c4c4c4; }"
            "QTableView { gridline-color: #b5b5b5; }"
        );
    }
}
