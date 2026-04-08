#pragma once

#include <QString>
#include <QDateTime>
#include <QTime>
#include <QList>
#include <QSet>
#include <QUuid>

struct ScheduleRule {
    enum Type { Periodic, FixedTime, Advanced };
    Type type = Periodic;
    int intervalSecs = 3600;
    QTime fixedTime;
    QSet<int> daysOfWeek;   // 1=Mon..7=Sun, empty=every day
    QDateTime nextTrigger;

    // Advanced 类型专用字段
    int advMinute = 0;          // 0-59
    int advHour = -1;           // -1=每小时, 0-23
    int advDay = -1;            // -1=每天, 1-31
    int advMonth = -1;          // -1=每月, 1-12
    int advYear = -1;           // -1=每年, 具体年份如2026
    QSet<int> advDaysOfWeek;    // 1=Mon..7=Sun, empty=不限
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
    QString id;
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
    bool runHideWindow = false;
    bool lastRunHidden = false;
    bool trackRunDuration = false;

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
