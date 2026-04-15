#include "AppStorage.h"
#include <QCoreApplication>
#include <QDir>

QString appRootPath() {
    return QCoreApplication::applicationDirPath();
}

QString appDataDirPath() {
    return QDir(appRootPath()).filePath("data");
}

void initializeAppStorage() {
    QDir().mkpath(appDataDirPath());
}
