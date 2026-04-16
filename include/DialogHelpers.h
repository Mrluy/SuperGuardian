#pragma once

#include <QtWidgets>
#include "GuardItem.h"

using namespace Qt::Literals::StringLiterals;

// 自定义日历网格控件（无空行问题）
class SimpleCalendarGrid : public QWidget {
public:
    explicit SimpleCalendarGrid(bool isDark, QWidget* parent = nullptr);
    int yearShown() const { return m_year; }
    int monthShown() const { return m_month; }
    void setCurrentPage(int year, int month);
    void setDateTextFormat(const QDate& date, const QTextCharFormat& format);
    QTextCharFormat dateTextFormat(const QDate& date) const;
    void setPageChangedCallback(std::function<void(int, int)> cb) { m_pageChangedCbs.append(std::move(cb)); }
private:
    void rebuild();
    int m_year, m_month;
    bool m_isDark;
    QGridLayout* m_grid;
    QLabel* m_cells[42]; // 6 rows x 7 cols
    QMap<QDate, QTextCharFormat> m_formats;
    QList<std::function<void(int, int)>> m_pageChangedCbs;
};

// 自定义年份选择下拉框，弹出时当前年份显示在第3行
class YearComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;
    void showPopup() override;
};

// 带自定义导航栏的日历控件返回结构
struct CalendarWithNav {
    QWidget* widget;
    SimpleCalendarGrid* calendar;
    QComboBox* yearCombo;
    QComboBox* monthCombo;
};

inline constexpr Qt::WindowFlags kDialogFlags = Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint;

bool showMessageDialog(QWidget* parent, const QString& title, const QString& text, bool isQuestion = false);
QString showItemDialog(QWidget* parent, const QString& title, const QString& label,
                       const QStringList& items, bool* ok);
int showIntDialog(QWidget* parent, const QString& title, const QString& label,
                  int value, int minVal, int maxVal, int step, bool* ok);
QString formatRestartInterval(int secs);
QString formatScheduleRules(const QList<ScheduleRule>& rules);
QString formatScheduleRulesDetail(const QList<ScheduleRule>& rules);
QString formatAdvancedRule(const ScheduleRule& r);
QString formatDaysShort(const QSet<int>& days);
QDateTime calculateNextTrigger(const ScheduleRule& rule, const QDateTime& from = QDateTime::currentDateTime());
QDateTime nextTriggerTime(const QList<ScheduleRule>& rules);
QList<QDateTime> computeTriggersInMonth(const ScheduleRule& rule, int year, int month, int maxCount = 500);
QSet<QDate> triggerDatesInMonth(const ScheduleRule& rule, int year, int month);
CalendarWithNav createCalendarWithNav(bool isDark, QWidget* parent = nullptr);
