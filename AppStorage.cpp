#include "AppStorage.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

QString appRootPath() {
    return QCoreApplication::applicationDirPath();
}

QString appConfigDirPath() {
    return QDir(appRootPath()).filePath("Config");
}

QString appLogsDirPath() {
    return QDir(appRootPath()).filePath("Logs");
}

QString appCacheDirPath() {
    return QDir(appRootPath()).filePath("Cache");
}

QString appSettingsFilePath() {
    return QDir(appConfigDirPath()).filePath("config.ini");
}

void initializeAppStorage() {
    QDir().mkpath(appConfigDirPath());
    QDir().mkpath(appLogsDirPath());
    QDir().mkpath(appCacheDirPath());
}

QString watchdogLogFilePath() {
    QDir dir(appLogsDirPath());
    dir.mkpath(".");
    return dir.filePath("watchdog.log");
}

void appendWatchdogLog(const QString& msg, unsigned long err) {
    QString logPath = watchdogLogFilePath();
    if (logPath.isEmpty()) return;
    QFile f(logPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " " << msg;
    if (err != 0) ts << " (err=" << err << ")";
    ts << "\n";
}

QString operationLogFilePath() {
    QDir dir(appLogsDirPath());
    dir.mkpath(".");
    return dir.filePath("operation.log");
}

void appendOperationLog(const QString& msg) {
    QString logPath = operationLogFilePath();
    if (logPath.isEmpty()) return;
    QFile f(logPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " " << msg << "\n";
}
