#pragma once

#include <QStringList>

class QWidget;
class QLabel;
class QLineEdit;
class DesktopSelectTable;

// ---- 白名单路径处理 ----

QString normalizeWhitelistPath(const QString& rawPath);
QString whitelistNameForPath(const QString& path);

// ---- 表格行操作 ----

QList<int> selectedWhitelistRows(DesktopSelectTable* table);
bool containsWhitelistPath(DesktopSelectTable* table, const QString& path, int skipRow = -1);
void updateWhitelistRow(DesktopSelectTable* table, int row, const QString& path);
bool addWhitelistPath(DesktopSelectTable* table, const QString& rawPath);

// ---- 批量操作 ----

int addWhitelistPaths(DesktopSelectTable* table, QLabel* statsLabel, QLineEdit* searchEdit,
    const QStringList& paths, bool replaceExisting, bool* updatingTable);
QStringList collectWhitelistPaths(DesktopSelectTable* table);
QStringList collectWhitelistPaths(DesktopSelectTable* table, const QList<int>& rows);

// ---- 搜索过滤 ----

void refreshWhitelistFilter(DesktopSelectTable* table, QLabel* statsLabel, const QString& keyword);

// ---- 导入导出 ----

QStringList importWhitelistEntries(const QString& filePath);
bool exportWhitelistEntries(const QString& filePath, const QStringList& paths);

// ---- UI 辅助 ----

int promptWhitelistImportMode(QWidget* parent, const QString& title);
QString buildDropConfirmText(const QStringList& files);
