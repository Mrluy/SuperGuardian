#include "ConfigDatabase.h"
#include "AppStorage.h"
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>

using namespace Qt::Literals::StringLiterals;

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

void ConfigDatabase::migrateFromIni(const QString& iniPath) {
    if (!m_opened) return;
    if (!QFile::exists(iniPath)) return;

    // 检查是否已有数据（避免重复迁移）
    QSqlQuery check(m_db);
    check.exec(u"SELECT COUNT(*) FROM config"_s);
    if (check.next() && check.value(0).toInt() > 0) return;

    QSettings s(iniPath, QSettings::IniFormat);

    QSqlQuery q(m_db);
    q.exec(u"BEGIN TRANSACTION"_s);

    auto store = [&](const QString& key, const QVariant& val) {
        q.prepare(u"INSERT OR REPLACE INTO config (key, value) VALUES (?, ?)"_s);
        q.addBindValue(key);
        QJsonObject wrapper;
        wrapper[u"v"_s] = QJsonValue::fromVariant(val);
        q.addBindValue(QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact)));
        q.exec();
    };

    // 迁移 SMTP 配置
    s.beginGroup("smtp");
    store(u"smtp/server"_s, s.value("server"));
    store(u"smtp/port"_s, s.value("port", 587));
    store(u"smtp/useTls"_s, s.value("useTls", true));
    store(u"smtp/username"_s, s.value("username"));
    store(u"smtp/password"_s, s.value("password"));
    store(u"smtp/fromAddress"_s, s.value("fromAddress"));
    store(u"smtp/fromName"_s, s.value("fromName"));
    store(u"smtp/toAddress"_s, s.value("toAddress"));
    s.endGroup();

    store(u"emailEnabled"_s, s.value("emailEnabled", false));
    store(u"duplicateWhitelist"_s, s.value("duplicateWhitelist"));
    store(u"theme"_s, s.value("theme"));
    store(u"minimizeToTray"_s, s.value("minimizeToTray", false));
    store(u"self_guard_manual_exit"_s, s.value("self_guard_manual_exit"));
    store(u"watchdog_pid"_s, s.value("watchdog_pid", 0));

    // 迁移 items 数组 — 读取为 JSON 数组存储
    int size = s.beginReadArray("items");
    QJsonArray itemsArr;
    for (int i = 0; i < size; ++i) {
        s.setArrayIndex(i);
        QJsonObject item;
        for (const QString& key : s.childKeys()) {
            item[key] = QJsonValue::fromVariant(s.value(key));
        }
        itemsArr.append(item);
    }
    s.endArray();
    store(u"items"_s, QString::fromUtf8(QJsonDocument(itemsArr).toJson(QJsonDocument::Compact)));

    // 迁移列宽等 UI 设置
    if (s.contains("columnWidths"))
        store(u"columnWidths"_s, s.value("columnWidths"));
    if (s.contains("sortSection"))
        store(u"sortSection"_s, s.value("sortSection"));
    if (s.contains("sortState"))
        store(u"sortState"_s, s.value("sortState"));
    if (s.contains("hiddenColumns"))
        store(u"hiddenColumns"_s, s.value("hiddenColumns"));
    if (s.contains("headerOrder"))
        store(u"headerOrder"_s, s.value("headerOrder"));

    q.exec(u"COMMIT"_s);
}
