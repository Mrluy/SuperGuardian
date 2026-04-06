#pragma once

#include <QtWidgets>
#include "GuardItem.h"

using namespace Qt::Literals::StringLiterals;

inline constexpr Qt::WindowFlags kDialogFlags = Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint;

bool showMessageDialog(QWidget* parent, const QString& title, const QString& text, bool isQuestion = false);
QString showItemDialog(QWidget* parent, const QString& title, const QString& label,
                       const QStringList& items, bool* ok);
int showIntDialog(QWidget* parent, const QString& title, const QString& label,
                  int value, int minVal, int maxVal, int step, bool* ok);
QString formatRestartInterval(int secs);
QString formatScheduleRules(const QList<ScheduleRule>& rules);
QString formatAdvancedRule(const ScheduleRule& r);
QString formatDaysShort(const QSet<int>& days);
QDateTime calculateNextTrigger(const ScheduleRule& rule, const QDateTime& from = QDateTime::currentDateTime());
QDateTime nextTriggerTime(const QList<ScheduleRule>& rules);
