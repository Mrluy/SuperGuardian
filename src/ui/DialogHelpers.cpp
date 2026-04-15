#include "DialogHelpers.h"

bool showMessageDialog(QWidget* parent, const QString& title, const QString& text, bool isQuestion) {
    QDialog dlg(parent, kDialogFlags);
    dlg.setWindowTitle(title);
    dlg.setFixedSize(300, 200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QLabel* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl, 1);
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnLay->addWidget(okBtn);
    if (isQuestion) {
        QPushButton* cancelBtn = new QPushButton(u"取消"_s);
        QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLay->addWidget(cancelBtn);
    }
    btnLay->addStretch();
    lay->addLayout(btnLay);
    return dlg.exec() == QDialog::Accepted;
}

QString showItemDialog(QWidget* parent, const QString& title, const QString& label,
                              const QStringList& items, bool* ok) {
    QDialog dlg(parent, kDialogFlags);
    dlg.setWindowTitle(title);
    dlg.setFixedSize(300, 200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(label));
    QComboBox* combo = new QComboBox();
    combo->addItems(items);
    lay->addWidget(combo);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    int result = dlg.exec();
    if (ok) *ok = (result == QDialog::Accepted);
    return combo->currentText();
}

int showIntDialog(QWidget* parent, const QString& title, const QString& label,
                         int value, int minVal, int maxVal, int step, bool* ok) {
    QDialog dlg(parent, kDialogFlags);
    dlg.setWindowTitle(title);
    dlg.setFixedSize(300, 200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(label));
    QSpinBox* spin = new QSpinBox();
    spin->setRange(minVal, maxVal);
    spin->setValue(value);
    spin->setSingleStep(step);
    lay->addWidget(spin);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    int result = dlg.exec();
    if (ok) *ok = (result == QDialog::Accepted);
    return spin->value();
}

QString formatRestartInterval(int secs) {
    if (secs <= 0) return u"-"_s;
    int days = secs / 86400;
    int hours = (secs % 86400) / 3600;
    int mins = (secs % 3600) / 60;
    int remainSecs = secs % 60;
    QString detail;
    if (days > 0) detail += QString::number(days) + u"天"_s;
    if (hours > 0) detail += QString::number(hours) + u"小时"_s;
    if (mins > 0) detail += QString::number(mins) + u"分钟"_s;
    if (remainSecs > 0) detail += QString::number(remainSecs) + u"秒"_s;
    if (detail.isEmpty()) detail = u"0秒"_s;
    return u"周期 "_s + detail;
}

QString formatDaysShort(const QSet<int>& days) {
    if (days.isEmpty()) return u"每天"_s;
    static const QSet<int> weekdays = {1,2,3,4,5};
    static const QSet<int> weekend = {6,7};
    if (days == weekdays) return u"工作日"_s;
    if (days == weekend) return u"周末"_s;
    static constexpr QStringView names[] = { {}, u"周一", u"周二", u"周三", u"周四", u"周五", u"周六", u"周日" };
    QList<int> sorted(days.cbegin(), days.cend());
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    for (int d : sorted) {
        if (d >= 1 && d <= 7) parts << names[d].toString();
    }
    return parts.join(u","_s);
}

QDateTime calculateNextTrigger(const ScheduleRule& rule, const QDateTime& from) {
    if (rule.type == ScheduleRule::Periodic) {
        return from.addSecs(rule.intervalSecs);
    }
    if (rule.type == ScheduleRule::Advanced) {
        QDateTime candidate = from.addSecs(1);
        for (int attempt = 0; attempt < 366 * 24 * 60; ++attempt) {
            QDate d = candidate.date();
            QTime t = candidate.time();
            if (rule.advYear > 0 && d.year() != rule.advYear) {
                if (d.year() > rule.advYear) return QDateTime();
                candidate = QDateTime(QDate(rule.advYear, 1, 1), QTime(0, 0, 0));
                continue;
            }
            if (rule.advMonth > 0 && d.month() != rule.advMonth) {
                int targetMonth = rule.advMonth;
                int targetYear = d.year();
                if (d.month() > targetMonth) targetYear++;
                if (rule.advYear > 0 && targetYear != rule.advYear) return QDateTime();
                candidate = QDateTime(QDate(targetYear, targetMonth, 1), QTime(0, 0, 0));
                continue;
            }
            if (rule.advDay > 0 && d.day() != rule.advDay) {
                int targetDay = rule.advDay;
                QDate next = d;
                if (d.day() > targetDay) {
                    next = d.addMonths(1);
                    next = QDate(next.year(), next.month(), 1);
                }
                int maxDay = QDate(next.year(), next.month(), 1).daysInMonth();
                if (targetDay > maxDay) { candidate = QDateTime(next.addMonths(1), QTime(0, 0, 0)); continue; }
                candidate = QDateTime(QDate(next.year(), next.month(), targetDay), QTime(0, 0, 0));
                continue;
            }
            if (!rule.advDaysOfWeek.isEmpty() && !rule.advDaysOfWeek.contains(d.dayOfWeek())) {
                candidate = QDateTime(d.addDays(1), QTime(0, 0, 0));
                continue;
            }
            if (rule.advHour >= 0 && t.hour() != rule.advHour) {
                if (t.hour() > rule.advHour) {
                    candidate = QDateTime(d.addDays(1), QTime(rule.advHour, 0, 0));
                } else {
                    candidate = QDateTime(d, QTime(rule.advHour, 0, 0));
                }
                continue;
            }
            if (rule.advMinute >= 0 && t.minute() != rule.advMinute) {
                if (t.minute() > rule.advMinute) {
                    if (rule.advHour >= 0) {
                        candidate = QDateTime(d.addDays(1), QTime(rule.advHour, rule.advMinute, 0));
                    } else {
                        candidate = QDateTime(d, QTime(t.hour(), rule.advMinute, 0)).addSecs(3600);
                    }
                } else {
                    candidate = QDateTime(d, QTime(t.hour(), rule.advMinute, 0));
                }
                continue;
            }
            if (rule.advSecond >= 0 && t.second() != rule.advSecond) {
                int finalMin = (rule.advMinute >= 0) ? rule.advMinute : t.minute();
                if (t.second() > rule.advSecond) {
                    if (rule.advMinute >= 0) {
                        if (rule.advHour >= 0) {
                            candidate = QDateTime(d.addDays(1), QTime(rule.advHour, rule.advMinute, rule.advSecond));
                        } else {
                            candidate = QDateTime(d, QTime(t.hour(), finalMin, rule.advSecond)).addSecs(3600);
                        }
                    } else {
                        candidate = QDateTime(d, QTime(t.hour(), t.minute(), rule.advSecond)).addSecs(60);
                    }
                } else {
                    candidate = QDateTime(d, QTime(t.hour(), finalMin, rule.advSecond));
                }
                continue;
            }
            int finalMin = (rule.advMinute >= 0) ? rule.advMinute : t.minute();
            int finalSec = (rule.advSecond >= 0) ? rule.advSecond : t.second();
            return QDateTime(d, QTime(t.hour(), finalMin, finalSec));
        }
        return QDateTime();
    }
    // FixedTime: find next matching day+time
    QDateTime candidate(from.date(), rule.fixedTime);
    if (candidate <= from) candidate = candidate.addDays(1);
    if (rule.daysOfWeek.isEmpty()) return candidate;
    for (int attempt = 0; attempt < 8; ++attempt) {
        int dow = candidate.date().dayOfWeek();
        if (rule.daysOfWeek.contains(dow)) return candidate;
        candidate = candidate.addDays(1);
    }
    return candidate;
}

QDateTime nextTriggerTime(const QList<ScheduleRule>& rules) {
    QDateTime earliest;
    for (const ScheduleRule& r : rules) {
        if (r.nextTrigger.isValid()) {
            if (!earliest.isValid() || r.nextTrigger < earliest)
                earliest = r.nextTrigger;
        }
    }
    return earliest;
}

QList<QDateTime> computeTriggersInMonth(const ScheduleRule& rule, int year, int month, int maxCount) {
    QList<QDateTime> times;
    QDate firstDay(year, month, 1);
    if (!firstDay.isValid()) return times;
    QDateTime monthStart(firstDay, QTime(0, 0, 0));
    QDateTime monthEnd(QDate(year, month, firstDay.daysInMonth()), QTime(23, 59, 59));
    QDateTime now = QDateTime::currentDateTime();

    if (rule.type == ScheduleRule::Periodic) {
        if (rule.intervalSecs <= 0) return times;
        if (monthEnd < now) return times;
        qint64 interval = static_cast<qint64>(rule.intervalSecs);
        // 计算第一个落在目标月内的触发序号 k (触发时间 = now + k * interval)
        qint64 secsToMonthStart = now.secsTo(monthStart);
        qint64 minK = (secsToMonthStart > 0)
            ? (secsToMonthStart + interval - 1) / interval
            : 1;
        if (minK < 1) minK = 1;
        for (qint64 k = minK; times.size() < maxCount; ++k) {
            QDateTime trigger = now.addSecs(k * interval);
            if (trigger > monthEnd) break;
            times.append(trigger);
        }
    } else if (rule.type == ScheduleRule::FixedTime) {
        for (int day = 1; day <= firstDay.daysInMonth(); ++day) {
            QDate d(year, month, day);
            QDateTime dt(d, rule.fixedTime);
            if (dt <= now) continue;
            if (!rule.daysOfWeek.isEmpty() && !rule.daysOfWeek.contains(d.dayOfWeek()))
                continue;
            times.append(dt);
            if (times.size() >= maxCount) break;
        }
    } else if (rule.type == ScheduleRule::Advanced) {
        QDateTime cur = (monthStart > now) ? monthStart.addSecs(-1) : now;
        for (int safety = 0; safety < maxCount * 2 + 10000; ++safety) {
            QDateTime next = calculateNextTrigger(rule, cur);
            if (!next.isValid() || next > monthEnd) break;
            if (next >= monthStart) {
                times.append(next);
                if (times.size() >= maxCount) break;
            }
            cur = next;
        }
    }
    return times;
}

QString formatAdvancedRule(const ScheduleRule& r) {
    QString result;
    if (r.advYear > 0)
        result += u"%1年 "_s.arg(r.advYear);
    else
        result += u"每年 "_s;
    if (r.advMonth > 0)
        result += u"%1月 "_s.arg(r.advMonth);
    else
        result += u"每月 "_s;
    if (r.advDay > 0) {
        result += u"%1日"_s.arg(r.advDay);
        if (!r.advDaysOfWeek.isEmpty())
            result += u"&"_s + formatDaysShort(r.advDaysOfWeek);
        result += u" "_s;
    } else if (!r.advDaysOfWeek.isEmpty()) {
        result += formatDaysShort(r.advDaysOfWeek) + u" "_s;
    } else {
        result += u"每天 "_s;
    }
    if (r.advHour >= 0 && r.advMinute >= 0 && r.advSecond >= 0)
        result += u"%1时%2分%3秒"_s.arg(r.advHour, 2, 10, QChar('0')).arg(r.advMinute, 2, 10, QChar('0')).arg(r.advSecond, 2, 10, QChar('0'));
    else if (r.advHour >= 0 && r.advMinute >= 0)
        result += u"%1时%2分"_s.arg(r.advHour, 2, 10, QChar('0')).arg(r.advMinute, 2, 10, QChar('0'));
    else if (r.advHour >= 0 && r.advSecond >= 0)
        result += u"%1时 每分钟 %2秒"_s.arg(r.advHour, 2, 10, QChar('0')).arg(r.advSecond, 2, 10, QChar('0'));
    else if (r.advMinute >= 0 && r.advSecond >= 0)
        result += u"每小时 %1分%2秒"_s.arg(r.advMinute, 2, 10, QChar('0')).arg(r.advSecond, 2, 10, QChar('0'));
    else if (r.advHour >= 0)
        result += u"%1时 每分钟"_s.arg(r.advHour, 2, 10, QChar('0'));
    else if (r.advMinute >= 0)
        result += u"每小时 %1分"_s.arg(r.advMinute, 2, 10, QChar('0'));
    else if (r.advSecond >= 0)
        result += u"每分钟 %1秒"_s.arg(r.advSecond, 2, 10, QChar('0'));
    else
        result += u"每秒"_s;
    return result;
}

QString formatScheduleRules(const QList<ScheduleRule>& rules) {
    if (rules.isEmpty()) return u"-"_s;
    if (rules.size() == 1) {
        const ScheduleRule& r = rules[0];
        if (r.type == ScheduleRule::Periodic) return formatRestartInterval(r.intervalSecs);
        if (r.type == ScheduleRule::Advanced) return formatAdvancedRule(r);
        return formatDaysShort(r.daysOfWeek) + u" "_s + r.fixedTime.toString(u"HH:mm:ss"_s);
    }
    return u"%1个规则"_s.arg(rules.size());
}

QString formatScheduleRulesDetail(const QList<ScheduleRule>& rules) {
    if (rules.isEmpty()) return u"-"_s;
    QStringList lines;
    for (int i = 0; i < rules.size(); ++i) {
        const ScheduleRule& r = rules[i];
        QString text;
        if (r.type == ScheduleRule::Periodic)
            text = formatRestartInterval(r.intervalSecs);
        else if (r.type == ScheduleRule::Advanced)
            text = formatAdvancedRule(r);
        else
            text = formatDaysShort(r.daysOfWeek) + u" "_s + r.fixedTime.toString(u"HH:mm:ss"_s);
        lines << u"[%1] %2"_s.arg(i + 1).arg(text);
    }
    return lines.join(u"\n"_s);
}
