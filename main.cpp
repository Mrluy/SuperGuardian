#include <QtWidgets/QApplication>
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtSql/QSqlDatabase>
#include "SuperGuardian.h"
#include "AppStorage.h"
#include "ProcessUtils.h"
#include "ConfigDatabase.h"
#include <windows.h>

// 静态构建需要显式导入 SQLite 插件
Q_IMPORT_PLUGIN(QSQLiteDriverPlugin)

using namespace Qt::Literals::StringLiterals;

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName(u"SuperGuardian"_s);
    QCoreApplication::setOrganizationName(u"SuperGuardian"_s);
    QCoreApplication::setApplicationVersion(u"1.0.1"_s);

    if (argc > 1 && QByteArrayView(argv[1]) == "--watchdog") {
        QCoreApplication app(argc, argv);
        initializeAppStorage();
        return runWatchdogMode(argc, argv);
    }

    bool isRestart = false;
    for (int i = 1; i < argc; i++) {
        if (QByteArrayView(argv[i]) == "--restart") {
            isRestart = true;
            break;
        }
    }

    QApplication a(argc, argv);
    initializeAppStorage();

    if (isRestart) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            QSharedMemory test("SuperGuardianSingleton");
            if (test.create(1)) { test.detach(); break; }
            QThread::msleep(250);
        }
    }

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
    if (!ConfigDatabase::instance().value(u"minimizeToTray"_s, false).toBool())
        w.show();
    return a.exec();
}
