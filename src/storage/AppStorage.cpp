#include "AppStorage.h"
#include <QCoreApplication>
#include <QDir>

QString appRootPath() {
    return QCoreApplication::applicationDirPath();
}

QString appDataDirPath() {
    return QDir(appRootPath()).filePath("data");
}

QString appSettingsFilePath() {
    // 保留旧路径以支持迁移
    return QDir(appRootPath()).filePath("Config/config.ini");
}

void initializeAppStorage() {
    QDir().mkpath(appDataDirPath());
}
