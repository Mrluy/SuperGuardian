#pragma once

#include <QString>
#include <QDateTime>
#include <QList>
#include <QSqlDatabase>

struct LogEntry {
    qint64 id = 0;
    QDateTime timestamp;
    QString category;
    QString program;
    QString message;
};

class LogDatabase {
public:
    static LogDatabase& instance();

    void addLog(const QString& category, const QString& message, const QString& program = QString());
    QList<LogEntry> queryLogs(const QString& category, int limit = 500, int offset = 0) const;
    int logCount(const QString& category) const;
    void clearLogs(const QString& category);

private:
    LogDatabase();
    ~LogDatabase();
    LogDatabase(const LogDatabase&) = delete;
    LogDatabase& operator=(const LogDatabase&) = delete;

    void open();
    void trimIfNeeded(const QString& category);

    QSqlDatabase m_db;
    bool m_opened = false;

    static constexpr int MaxRecords = 100000;
    static constexpr int TrimThreshold = 100500;
    static constexpr int TrimCount = 500;
};

// 便捷函数
void logOperation(const QString& message, const QString& program = QString());
void logRuntime(const QString& message, const QString& program = QString());
void logGuard(const QString& message, const QString& program = QString());
void logScheduledRestart(const QString& message, const QString& program = QString());
void logScheduledRun(const QString& message, const QString& program = QString());

// 程序标识辅助：生成 "进程名 [参数]" 格式
QString programId(const QString& processName, const QString& args);
