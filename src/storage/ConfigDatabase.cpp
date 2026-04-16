#include "ConfigDatabase.h"
#include "LogDatabase.h"
#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>

using namespace Qt::Literals::StringLiterals;

// ── AppStorage ──

QString appRootPath() {
    return QCoreApplication::applicationDirPath();
}

QString appDataDirPath() {
    return QDir(appRootPath()).filePath("data");
}

void initializeAppStorage() {
    QDir().mkpath(appDataDirPath());
}

// ── ConfigDatabase ──

ConfigDatabase& ConfigDatabase::instance() {
    static ConfigDatabase inst;
    return inst;
}

ConfigDatabase::ConfigDatabase() {
    open();
}

ConfigDatabase::~ConfigDatabase() {
    if (m_db.isOpen())
        m_db.close();
}

void ConfigDatabase::open() {
    if (m_opened) return;

    QString dbPath = QDir(appDataDirPath()).filePath(u"config.db"_s);
    m_db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"config_conn"_s);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) return;

    QSqlQuery q(m_db);
    q.exec(u"PRAGMA journal_mode=WAL"_s);
    q.exec(u"PRAGMA synchronous=NORMAL"_s);

    q.exec(u"CREATE TABLE IF NOT EXISTS config ("
           "key TEXT PRIMARY KEY, "
           "value TEXT)"_s);

    m_opened = true;
}

void ConfigDatabase::setValue(const QString& key, const QVariant& value) {
    if (!m_opened) return;

    QSqlQuery q(m_db);
    q.prepare(u"INSERT OR REPLACE INTO config (key, value) VALUES (?, ?)"_s);
    q.addBindValue(key);

    // 将 QVariant 序列化为 JSON 字符串以保留类型信息
    QJsonValue jv = QJsonValue::fromVariant(value);
    QJsonObject wrapper;
    wrapper[u"v"_s] = jv;
    q.addBindValue(QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact)));
    q.exec();
}

QVariant ConfigDatabase::value(const QString& key, const QVariant& defaultValue) const {
    if (!m_opened) return defaultValue;

    QSqlQuery q(m_db);
    q.prepare(u"SELECT value FROM config WHERE key = ?"_s);
    q.addBindValue(key);
    q.exec();

    if (!q.next()) return defaultValue;

    QString raw = q.value(0).toString();
    QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (doc.isObject() && doc.object().contains(u"v"_s)) {
        return doc.object()[u"v"_s].toVariant();
    }
    // 兼容纯文本值
    return raw;
}

bool ConfigDatabase::contains(const QString& key) const {
    if (!m_opened) return false;
    QSqlQuery q(m_db);
    q.prepare(u"SELECT 1 FROM config WHERE key = ?"_s);
    q.addBindValue(key);
    q.exec();
    return q.next();
}

void ConfigDatabase::remove(const QString& key) {
    if (!m_opened) return;
    QSqlQuery q(m_db);
    q.prepare(u"DELETE FROM config WHERE key = ?"_s);
    q.addBindValue(key);
    q.exec();
}

void ConfigDatabase::beginBatch() {
    if (!m_opened || m_inBatch) return;
    QSqlQuery q(m_db);
    q.exec(u"BEGIN TRANSACTION"_s);
    m_inBatch = true;
}

void ConfigDatabase::endBatch() {
    if (!m_opened || !m_inBatch) return;
    QSqlQuery q(m_db);
    q.exec(u"COMMIT"_s);
    m_inBatch = false;
}

QJsonObject ConfigDatabase::exportToJson() const {
    QJsonObject result;
    if (!m_opened) return result;

    QSqlQuery q(m_db);
    q.exec(u"SELECT key, value FROM config"_s);
    while (q.next()) {
        QString key = q.value(0).toString();
        QString raw = q.value(1).toString();
        QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isObject() && doc.object().contains(u"v"_s)) {
            result[key] = doc.object()[u"v"_s];
        } else {
            result[key] = raw;
        }
    }
    return result;
}

void ConfigDatabase::importFromJson(const QJsonObject& json) {
    if (!m_opened) return;

    QSqlQuery q(m_db);
    q.exec(u"BEGIN TRANSACTION"_s);
    q.exec(u"DELETE FROM config"_s);

    q.prepare(u"INSERT INTO config (key, value) VALUES (?, ?)"_s);
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        q.addBindValue(it.key());
        QJsonObject wrapper;
        wrapper[u"v"_s] = it.value();
        q.addBindValue(QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact)));
        q.exec();
    }

    q.exec(u"COMMIT"_s);
}

// ---- LogDatabase ----

static QMutex s_logMutex;

LogDatabase& LogDatabase::instance() {
    static LogDatabase inst;
    return inst;
}

LogDatabase::LogDatabase() {
    open();
}

LogDatabase::~LogDatabase() {
    if (m_db.isOpen())
        m_db.close();
}

void LogDatabase::open() {
    if (m_opened) return;

    QString dbPath = QDir(appDataDirPath()).filePath(u"logs.db"_s);
    m_db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"logs_conn"_s);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) return;

    QSqlQuery q(m_db);
    q.exec(u"PRAGMA journal_mode=WAL"_s);
    q.exec(u"PRAGMA synchronous=NORMAL"_s);

    q.exec(u"CREATE TABLE IF NOT EXISTS logs ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT, "
           "timestamp TEXT NOT NULL, "
           "category TEXT NOT NULL, "
           "program TEXT, "
           "message TEXT NOT NULL)"_s);

    q.exec(u"CREATE INDEX IF NOT EXISTS idx_logs_category ON logs(category)"_s);
    q.exec(u"CREATE INDEX IF NOT EXISTS idx_logs_timestamp ON logs(category, timestamp DESC)"_s);

    m_opened = true;
}

void LogDatabase::addLog(const QString& category, const QString& message, const QString& program) {
    QMutexLocker locker(&s_logMutex);
    if (!m_opened) return;

    QSqlQuery q(m_db);
    q.prepare(u"INSERT INTO logs (timestamp, category, program, message) VALUES (?, ?, ?, ?)"_s);
    q.addBindValue(QDateTime::currentDateTime().toString(u"yyyy-MM-dd hh:mm:ss.zzz"_s));
    q.addBindValue(category);
    q.addBindValue(program.isEmpty() ? QString() : program);
    q.addBindValue(message);
    q.exec();

    trimIfNeeded(category);
}

QList<LogEntry> LogDatabase::queryLogs(const QString& category, int limit, int offset) const {
    QList<LogEntry> result;
    if (!m_opened) return result;

    QSqlQuery q(m_db);
    q.prepare(u"SELECT id, timestamp, category, program, message FROM logs "
              "WHERE category = ? ORDER BY id DESC LIMIT ? OFFSET ?"_s);
    q.addBindValue(category);
    q.addBindValue(limit);
    q.addBindValue(offset);
    q.exec();

    while (q.next()) {
        LogEntry e;
        e.id = q.value(0).toLongLong();
        e.timestamp = QDateTime::fromString(q.value(1).toString(), u"yyyy-MM-dd hh:mm:ss.zzz"_s);
        e.category = q.value(2).toString();
        e.program = q.value(3).toString();
        e.message = q.value(4).toString();
        result.append(e);
    }
    return result;
}

int LogDatabase::logCount(const QString& category) const {
    if (!m_opened) return 0;
    QSqlQuery q(m_db);
    q.prepare(u"SELECT COUNT(*) FROM logs WHERE category = ?"_s);
    q.addBindValue(category);
    q.exec();
    return q.next() ? q.value(0).toInt() : 0;
}

void LogDatabase::clearLogs(const QString& category) {
    QMutexLocker locker(&s_logMutex);
    if (!m_opened) return;
    QSqlQuery q(m_db);
    q.prepare(u"DELETE FROM logs WHERE category = ?"_s);
    q.addBindValue(category);
    q.exec();
}

void LogDatabase::trimIfNeeded(const QString& category) {
    QSqlQuery q(m_db);
    q.prepare(u"SELECT COUNT(*) FROM logs WHERE category = ?"_s);
    q.addBindValue(category);
    q.exec();
    if (!q.next()) return;

    int count = q.value(0).toInt();
    if (count <= TrimThreshold) return;

    q.prepare(u"DELETE FROM logs WHERE id IN ("
              "SELECT id FROM logs WHERE category = ? ORDER BY id ASC LIMIT ?)"_s);
    q.addBindValue(category);
    q.addBindValue(TrimCount);
    q.exec();
}

void logOperation(const QString& message, const QString& program) {
    LogDatabase::instance().addLog(u"operation"_s, message, program);
}

void logRuntime(const QString& message, const QString& program) {
    LogDatabase::instance().addLog(u"runtime"_s, message, program);
}

void logGuard(const QString& message, const QString& program) {
    LogDatabase::instance().addLog(u"guard"_s, message, program);
}

void logScheduledRestart(const QString& message, const QString& program) {
    LogDatabase::instance().addLog(u"scheduled_restart"_s, message, program);
}

void logScheduledRun(const QString& message, const QString& program) {
    LogDatabase::instance().addLog(u"scheduled_run"_s, message, program);
}

QString programId(const QString& processName, const QString& args) {
    if (args.isEmpty()) return processName;
    return processName + u" [%1]"_s.arg(args);
}
