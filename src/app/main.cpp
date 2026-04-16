#include <QtWidgets/QApplication>
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtSql/QSqlDatabase>
#include "SuperGuardian.h"
#include "ProcessUtils.h"
#include "ConfigDatabase.h"
#include <windows.h>

// 静态构建需要显式导入 SQLite 插件
Q_IMPORT_PLUGIN(QSQLiteDriverPlugin)

using namespace Qt::Literals::StringLiterals;

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName(u"SuperGuardian"_s);
    QCoreApplication::setOrganizationName(u"SuperGuardian"_s);
    QCoreApplication::setApplicationVersion(u"1.0.6"_s);

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

    // 检查重启原因并弹窗通知
    QTimer::singleShot(500, [&w]() {
        auto& db = ConfigDatabase::instance();
        QString reason = db.value(u"restart_reason"_s).toString();
        if (reason.isEmpty()) return;
        QString dumpPath = db.value(u"restart_dump_path"_s).toString();
        db.remove(u"restart_reason"_s);
        db.remove(u"restart_dump_path"_s);
        QString msg;
        if (reason == u"hang"_s) {
            msg = u"检测到程序发生【未响应】状态，已自动转储并重启。"_s;
            if (!dumpPath.isEmpty())
                msg += u"\n\n转储文件：\n"_s + dumpPath;
        } else {
            msg = u"检测到程序意外终止，已通过自我守护自动重启。"_s;
        }
        w.show();
        w.raise();
        w.activateWindow();
        QMessageBox::warning(&w, u"自动重启通知"_s, msg);
    });

    return a.exec();
}
