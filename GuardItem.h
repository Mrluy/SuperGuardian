#pragma once

#include <QString>
#include <QDateTime>

struct GuardItem {
    QString path;
    QString targetPath;
    QString processName;
    bool guarding = false;
    bool internalSelfGuard = false;
    QDateTime startTime;
    QDateTime lastRestart;
    QDateTime lastLaunchTime;
    QDateTime lastGuardRestartTime;
    int restartCount = 0;
    int scheduledRestartIntervalSecs = 0;
    QDateTime nextScheduledRestart;
    int guardDelaySecs = 0;
    QDateTime guardDelayExitTime;
};
