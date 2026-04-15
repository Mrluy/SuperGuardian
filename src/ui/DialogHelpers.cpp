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
    QStringList parts;
    if (r.advYear > 0) parts << u"%1年"_s.arg(r.advYear);
    if (r.advMonth > 0) parts << u"%1月"_s.arg(r.advMonth);
    if (r.advDay > 0) parts << u"%1日"_s.arg(r.advDay);
    if (!r.advDaysOfWeek.isEmpty()) parts << formatDaysShort(r.advDaysOfWeek);
    if (r.advHour >= 0) parts << u"%1时"_s.arg(r.advHour, 2, 10, QChar('0'));
    if (r.advMinute >= 0) parts << u"%1分"_s.arg(r.advMinute, 2, 10, QChar('0'));
    if (r.advSecond >= 0) parts << u"%1秒"_s.arg(r.advSecond, 2, 10, QChar('0'));
    if (parts.isEmpty()) return u"无计划"_s;
    return parts.join(u" "_s);
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

// ── MonthOnlyCalendar ──
void MonthOnlyCalendar::paintCell(QPainter* painter, const QRect& rect, QDate date) const {
    bool isCurrentMonth = (date.month() == monthShown() && date.year() == yearShown());
    if (!isCurrentMonth) {
        // 空格：不绘制任何内容，也不绘制边界线
        return;
    }

    // 获取该日期的 format（用于高亮触发日期）
    QTextCharFormat fmt = dateTextFormat(date);
    bool isToday = (date == QDate::currentDate());

    // 绘制背景
    if (fmt.hasProperty(QTextFormat::BackgroundBrush)) {
        painter->fillRect(rect, fmt.background());
    }

    // 绘制文字（统一颜色，不区分周末）
    if (fmt.hasProperty(QTextFormat::ForegroundBrush)) {
        painter->setPen(fmt.foreground().color());
    } else {
        bool isDark = palette().color(QPalette::Window).lightness() < 128;
        painter->setPen(isDark ? QColor(220, 220, 220) : QColor(30, 30, 30));
    }
    painter->drawText(rect, Qt::AlignCenter, QString::number(date.day()));

    // 今天：绘制底部亮条
    if (isToday) {
        bool isDark = palette().color(QPalette::Window).lightness() < 128;
        QColor barColor = isDark ? QColor(96, 205, 255) : QColor(0, 95, 183);
        int barH = 2;
        painter->fillRect(rect.left(), rect.bottom() - barH, rect.width(), barH, barColor);
    }
}

// ── YearComboBox ──
void YearComboBox::showPopup() {
    QComboBox::showPopup();
    if (QAbstractItemView* v = view()) {
        int scrollTo = qMax(0, currentIndex() - 2);
        v->scrollTo(model()->index(scrollTo, 0), QAbstractItemView::PositionAtTop);
    }
}

// ── 创建带自定义导航栏的日历控件 ──
CalendarWithNav createCalendarWithNav(bool isDark, QWidget* parent) {
    CalendarWithNav nav;
    nav.widget = new QWidget(parent);
    QVBoxLayout* vlay = new QVBoxLayout(nav.widget);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    // 自定义导航栏
    QHBoxLayout* navLay = new QHBoxLayout();
    navLay->setContentsMargins(0, 0, 0, 2);
    navLay->setSpacing(4);

    QPushButton* prevBtn = new QPushButton();
    QPushButton* nextBtn = new QPushButton();
    const QString prevIconPath = isDark ? u":/SuperGuardian/light/L.png"_s
                                        : u":/SuperGuardian/dark/L.png"_s;
    const QString nextIconPath = isDark ? u":/SuperGuardian/light/R.png"_s
                                        : u":/SuperGuardian/dark/R.png"_s;
    prevBtn->setIcon(QIcon(prevIconPath));
    nextBtn->setIcon(QIcon(nextIconPath));
    prevBtn->setIconSize(QSize(16, 16));
    nextBtn->setIconSize(QSize(16, 16));
    prevBtn->setFixedSize(28, 28);
    nextBtn->setFixedSize(28, 28);
    prevBtn->setFlat(true);
    nextBtn->setFlat(true);

    // 年份下拉
    nav.yearCombo = new YearComboBox();
    for (int y = 2000; y <= 2099; ++y)
        nav.yearCombo->addItem(u"%1年"_s.arg(y), y);
    nav.yearCombo->setMaxVisibleItems(12);

    // 月份下拉
    nav.monthCombo = new QComboBox();
    static constexpr QStringView monthNames[] = {
        u"1月", u"2月", u"3月", u"4月", u"5月", u"6月",
        u"7月", u"8月", u"9月", u"10月", u"11月", u"12月"
    };
    for (int m = 0; m < 12; ++m)
        nav.monthCombo->addItem(monthNames[m].toString(), m + 1);

    navLay->addWidget(prevBtn, 0);
    navLay->addWidget(nav.yearCombo, 1);
    navLay->addWidget(nav.monthCombo, 1);
    navLay->addWidget(nextBtn, 0);
    vlay->addLayout(navLay);

    // 日历
    nav.calendar = new MonthOnlyCalendar();
    nav.calendar->setNavigationBarVisible(false);
    nav.calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    nav.calendar->setSelectionMode(QCalendarWidget::NoSelection);

    // 去除周末红色：统一所有日期颜色
    QTextCharFormat weekdayFmt;
    weekdayFmt.setForeground(isDark ? QColor(220, 220, 220) : QColor(30, 30, 30));
    nav.calendar->setWeekdayTextFormat(Qt::Saturday, weekdayFmt);
    nav.calendar->setWeekdayTextFormat(Qt::Sunday, weekdayFmt);

    // 动态行数：根据月份天数计算需要几行，设置对应高度
    auto updateCalendarHeight = [cal = nav.calendar]() {
        QDate first(cal->yearShown(), cal->monthShown(), 1);
        int daysInMonth = first.daysInMonth();
        int startDow = first.dayOfWeek(); // 1=Mon .. 7=Sun
        int totalCells = (startDow - 1) + daysInMonth;
        int rows = (totalCells + 6) / 7;
        // 每行大约 24px + 表头约 24px
        int headerH = 24;
        int rowH = 24;
        cal->setFixedHeight(headerH + rows * rowH + 4);
    };
    updateCalendarHeight();

    // 隐藏网格线（由 paintCell 自行决定是否绘制边界）
    nav.calendar->setGridVisible(false);

    vlay->addWidget(nav.calendar);

    // 初始同步
    int curYear = nav.calendar->yearShown();
    int curMonth = nav.calendar->monthShown();
    nav.yearCombo->setCurrentIndex(qBound(0, curYear - 2000, 99));
    nav.monthCombo->setCurrentIndex(curMonth - 1);

    // 连接 ← → 按钮
    QObject::connect(prevBtn, &QPushButton::clicked, nav.calendar, [cal = nav.calendar]() {
        int y = cal->yearShown(), m = cal->monthShown();
        if (m == 1) { y--; m = 12; } else { m--; }
        if (y >= 2000 && y <= 2099) cal->setCurrentPage(y, m);
    });
    QObject::connect(nextBtn, &QPushButton::clicked, nav.calendar, [cal = nav.calendar]() {
        int y = cal->yearShown(), m = cal->monthShown();
        if (m == 12) { y++; m = 1; } else { m++; }
        if (y >= 2000 && y <= 2099) cal->setCurrentPage(y, m);
    });

    // 下拉变化 → 切换日历页面
    QObject::connect(nav.yearCombo, &QComboBox::currentIndexChanged, nav.calendar,
        [cal = nav.calendar, mc = nav.monthCombo](int idx) {
            cal->setCurrentPage(2000 + idx, mc->currentIndex() + 1);
        });
    QObject::connect(nav.monthCombo, &QComboBox::currentIndexChanged, nav.calendar,
        [cal = nav.calendar, yc = nav.yearCombo](int idx) {
            cal->setCurrentPage(yc->currentData().toInt(), idx + 1);
        });

    // 日历页面变化 → 同步下拉 + 更新行数高度（用 QSignalBlocker 防止递归）
    QObject::connect(nav.calendar, &QCalendarWidget::currentPageChanged, nav.widget,
        [yc = nav.yearCombo, mc = nav.monthCombo, updateCalendarHeight](int year, int month) {
            QSignalBlocker yb(yc);
            QSignalBlocker mb(mc);
            yc->setCurrentIndex(qBound(0, year - 2000, 99));
            mc->setCurrentIndex(month - 1);
            updateCalendarHeight();
        });

    return nav;
}

// ── 计算月内触发总次数（数学计算，不生成列表）──
int countTriggersInMonth(const ScheduleRule& rule, int year, int month) {
    QDate firstDay(year, month, 1);
    if (!firstDay.isValid()) return 0;
    QDateTime monthStart(firstDay, QTime(0, 0, 0));
    QDateTime monthEnd(QDate(year, month, firstDay.daysInMonth()), QTime(23, 59, 59));
    QDateTime now = QDateTime::currentDateTime();

    if (rule.type == ScheduleRule::Periodic) {
        if (rule.intervalSecs <= 0 || monthEnd < now) return 0;
        qint64 interval = static_cast<qint64>(rule.intervalSecs);
        qint64 secsToMonthStart = now.secsTo(monthStart);
        qint64 minK = (secsToMonthStart > 0) ? (secsToMonthStart + interval - 1) / interval : 1;
        if (minK < 1) minK = 1;
        qint64 secsToMonthEnd = now.secsTo(monthEnd);
        qint64 maxK = secsToMonthEnd / interval;
        return (maxK >= minK) ? static_cast<int>(qMin(maxK - minK + 1, (qint64)9999999)) : 0;
    } else if (rule.type == ScheduleRule::FixedTime) {
        int count = 0;
        for (int day = 1; day <= firstDay.daysInMonth(); ++day) {
            QDate d(year, month, day);
            QDateTime dt(d, rule.fixedTime);
            if (dt <= now) continue;
            if (!rule.daysOfWeek.isEmpty() && !rule.daysOfWeek.contains(d.dayOfWeek())) continue;
            count++;
        }
        return count;
    } else {
        QDateTime cur = (monthStart > now) ? monthStart.addSecs(-1) : now;
        int count = 0;
        for (int safety = 0; safety < 100000; ++safety) {
            QDateTime next = calculateNextTrigger(rule, cur);
            if (!next.isValid() || next > monthEnd) break;
            if (next >= monthStart) count++;
            cur = next;
        }
        return count;
    }
}

// ── 计算月内有触发的日期集合（不生成完整时间列表）──
QSet<QDate> triggerDatesInMonth(const ScheduleRule& rule, int year, int month) {
    QSet<QDate> dates;
    QDate firstDay(year, month, 1);
    if (!firstDay.isValid()) return dates;
    int daysInMonth = firstDay.daysInMonth();
    QDateTime monthStart(firstDay, QTime(0, 0, 0));
    QDateTime monthEnd(QDate(year, month, daysInMonth), QTime(23, 59, 59));
    QDateTime now = QDateTime::currentDateTime();

    if (rule.type == ScheduleRule::Periodic) {
        if (rule.intervalSecs <= 0 || monthEnd < now) return dates;
        qint64 interval = static_cast<qint64>(rule.intervalSecs);
        for (int day = 1; day <= daysInMonth; ++day) {
            QDate d(year, month, day);
            QDateTime dayStart(d, QTime(0, 0, 0));
            QDateTime dayEnd(d, QTime(23, 59, 59));
            if (dayEnd < now) continue;
            QDateTime effectiveStart = (dayStart > now) ? dayStart : now.addSecs(1);
            qint64 secsToStart = now.secsTo(effectiveStart);
            qint64 minK = (secsToStart + interval - 1) / interval;
            if (minK < 1) minK = 1;
            if (now.addSecs(minK * interval) <= dayEnd) dates.insert(d);
        }
    } else if (rule.type == ScheduleRule::FixedTime) {
        for (int day = 1; day <= daysInMonth; ++day) {
            QDate d(year, month, day);
            QDateTime dt(d, rule.fixedTime);
            if (dt <= now) continue;
            if (!rule.daysOfWeek.isEmpty() && !rule.daysOfWeek.contains(d.dayOfWeek())) continue;
            dates.insert(d);
        }
    } else {
        QDateTime cur = (monthStart > now) ? monthStart.addSecs(-1) : now;
        for (int safety = 0; safety < 100000; ++safety) {
            QDateTime next = calculateNextTrigger(rule, cur);
            if (!next.isValid() || next > monthEnd) break;
            if (next >= monthStart) dates.insert(next.date());
            if (dates.size() >= daysInMonth) break;
            cur = next;
        }
    }
    return dates;
}
