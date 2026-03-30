#pragma once

#include <QString>
#include <QDateTime>
#include <QTime>
#include <QList>
#include <QSet>

struct ScheduleRule {
    enum Type { Periodic, FixedTime };
    Type type = Periodic;
    int intervalSecs = 3600;
    QTime fixedTime;
    QSet<int> daysOfWeek;   // 1=Mon..7=Sun, empty=every day
    QDateTime nextTrigger;
};

struct RetryConfig {
    int retryIntervalSecs = 30;
    int maxRetries = 10;        // 0=unlimited
    int maxDurationSecs = 300;  // 0=unlimited
};

struct EmailNotifyConfig {
    bool enabled = false;
    bool onGuardTriggered = false;
    bool onStartFailed = true;
    bool onScheduledRestartFailed = true;
    bool onScheduledRunFailed = true;
    bool onProcessExited = false;
    bool onRetryExhausted = true;
};

struct GuardItem {
    QString path;
    QString targetPath;
    QString processName;
    QString launchArgs;
    QString note;
    bool guarding = false;
    bool internalSelfGuard = false;
    bool pinned = false;
    int insertionOrder = 0;
    QDateTime startTime;
    QDateTime guardStartTime;
    QDateTime lastRestart;
    QDateTime lastLaunchTime;
    QDateTime lastGuardRestartTime;
    int restartCount = 0;

    QList<ScheduleRule> restartRules;
    bool restartRulesActive = false;

    int startDelaySecs = 1;
    QDateTime startDelayExitTime;

    bool scheduledRunEnabled = false;
    QList<ScheduleRule> runRules;

    RetryConfig retryConfig;
    int currentRetryCount = 0;
    QDateTime retryStartTime;
    QDateTime lastRetryTime;
    bool retryActive = false;

    EmailNotifyConfig emailNotify;

    // 失败通知去重：成功前只提醒一次
    bool notifiedStartFailed = false;
    bool notifiedRestartFailed = false;
    bool notifiedRunFailed = false;
    bool notifiedRetryExhausted = false;
};
