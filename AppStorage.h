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
QString scheduledRestartLogFilePath();
void appendScheduledRestartLog(const QString& msg);
QString scheduledRunLogFilePath();
void appendScheduledRunLog(const QString& msg);
