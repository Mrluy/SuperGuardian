#pragma once

#include <QString>

void initializeAppStorage();
QString appRootPath();
QString appConfigDirPath();
QString appLogsDirPath();
QString appCacheDirPath();
QString appSettingsFilePath();
QString watchdogLogFilePath();
void appendWatchdogLog(const QString& msg, unsigned long err = 0);
QString operationLogFilePath();
void appendOperationLog(const QString& msg);
