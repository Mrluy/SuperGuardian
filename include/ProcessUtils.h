#pragma once

#include <QString>
#include <QHash>
#include <QIcon>
#include <QDateTime>

struct ProcessInfo {
    int count = 0;
    QDateTime startTime;
};

// 一次性快照获取所有进程信息（避免多次 CreateToolhelp32Snapshot）
QHash<QString, ProcessInfo> takeProcessSnapshot();

QString resolveShortcut(const QString& path, QString* outArgs = nullptr);
QIcon getFileIcon(const QString& path);
bool isProcessRunning(const QString& name, int& count);
QDateTime getProcessStartTime(const QString& processName);
bool launchProgram(const QString& path, const QString& args = QString(), bool hideWindow = false);
void setAutostart(bool enable);
void killProcessesByName(const QString& name);

int runWatchdogMode(int argc, char* argv[]);
