#pragma once

#include <QString>
#include <QVariant>
#include <QJsonObject>
#include <QSqlDatabase>

class ConfigDatabase {
public:
    static ConfigDatabase& instance();

    void setValue(const QString& key, const QVariant& value);
    QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    bool contains(const QString& key) const;
    void remove(const QString& key);

    // 批量操作（减少事务开销）
    void beginBatch();
    void endBatch();

    // 导出全部配置为 JSON
    QJsonObject exportToJson() const;
    // 从 JSON 导入全部配置（清除现有配置）
    void importFromJson(const QJsonObject& json);

private:
    ConfigDatabase();
    ~ConfigDatabase();
    ConfigDatabase(const ConfigDatabase&) = delete;
    ConfigDatabase& operator=(const ConfigDatabase&) = delete;

    void open();

    QSqlDatabase m_db;
    bool m_opened = false;
    bool m_inBatch = false;
};
