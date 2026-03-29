#pragma once

#include <QString>
#include <QIcon>

QString resolveShortcut(const QString& path, QString* outArgs = nullptr);
QIcon getFileIcon(const QString& path);
bool isProcessRunning(const QString& name, int& count);
bool launchProgram(const QString& path, const QString& args = QString());
void setAutostart(bool enable);
void killProcessesByName(const QString& name);

int runWatchdogMode(int argc, char* argv[]);
