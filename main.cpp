// simplified main: instantiate SuperGuardian window

#include <QtWidgets/QApplication>
#include <QtCore/QCoreApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include "SuperGuardian.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include <windows.h>

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName("SuperGuardian");
    QCoreApplication::setOrganizationName("SuperGuardian");

    if (argc > 1 && QString::fromUtf8(argv[1]) == "--watchdog") {
        QCoreApplication app(argc, argv);
        initializeAppStorage();
        return runWatchdogMode(argc, argv);
    }

    QApplication a(argc, argv);
    initializeAppStorage();

    QSharedMemory shared("SuperGuardianSingleton");
    if (!shared.create(1)) {
        HWND hwnd = FindWindowW(nullptr, L"\x8d85\x7ea7\x5b88\x62a4");
        if (hwnd) {
            static const UINT WM_SG_SHOW = RegisterWindowMessageW(L"SuperGuardianShowMainWindow");
            PostMessageW(hwnd, WM_SG_SHOW, 0, 0);
        }
        return 0;
    }

    SuperGuardian w;
    QSettings s(appSettingsFilePath(), QSettings::IniFormat);
    if (!s.value("minimizeToTray", false).toBool())
        w.show();
    return a.exec();
}
