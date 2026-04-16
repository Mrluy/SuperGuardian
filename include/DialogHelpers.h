#pragma once

#include <QtWidgets>
#include "GuardItem.h"

using namespace Qt::Literals::StringLiterals;

// 只显示当前月份日期的日历控件
class MonthOnlyCalendar : public QCalendarWidget {
public:
    using QCalendarWidget::QCalendarWidget;
protected:
    void paintCell(QPainter* painter, const QRect& rect, QDate date) const override;
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
    QCalendarWidget* calendar;
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
