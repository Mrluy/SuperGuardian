#include "ScheduleRuleEditor.h"
#include "DialogHelpers.h"
#include <QtWidgets>
#include <QTimeZone>

using namespace Qt::Literals::StringLiterals;

// 根据当前规则设置构建 ScheduleRule（不做校验）
static ScheduleRule buildRuleFromUI(
    QPushButton* periodicBtn, QPushButton* fixedBtn,
    QSpinBox* daySpin, QSpinBox* hourSpin, QSpinBox* minSpin, QSpinBox* secSpin,
    QTimeEdit* timeEdit, QCheckBox* dowChecks[7],
    QCheckBox* advYearCheck, QSpinBox* advYearSpin,
    QCheckBox* advMonthCheck, QSpinBox* advMonthSpin,
    QCheckBox* advDayCheck, QSpinBox* advDaySpin,
    QCheckBox* advDowChecks[7],
    QCheckBox* advHourCheck, QSpinBox* advHourSpin,
    QCheckBox* advMinCheck, QSpinBox* advMinSpin,
    QCheckBox* advSecCheck, QSpinBox* advSecSpin)
{
    ScheduleRule rule;
    if (periodicBtn->isChecked()) {
        rule.type = ScheduleRule::Periodic;
        rule.intervalSecs = daySpin->value() * 86400 + hourSpin->value() * 3600
                          + minSpin->value() * 60 + secSpin->value();
    } else if (fixedBtn->isChecked()) {
        rule.type = ScheduleRule::FixedTime;
        rule.fixedTime = timeEdit->time();
        for (int d = 0; d < 7; d++)
            if (dowChecks[d]->isChecked()) rule.daysOfWeek.insert(d + 1);
    } else {
        rule.type = ScheduleRule::Advanced;
        rule.advSecond = advSecCheck->isChecked() ? advSecSpin->value() : -1;
        rule.advMinute = advMinCheck->isChecked() ? advMinSpin->value() : -1;
        rule.advHour = advHourCheck->isChecked() ? advHourSpin->value() : -1;
        rule.advDay = advDayCheck->isChecked() ? advDaySpin->value() : -1;
        rule.advMonth = advMonthCheck->isChecked() ? advMonthSpin->value() : -1;
        rule.advYear = advYearCheck->isChecked() ? advYearSpin->value() : -1;
        for (int d = 0; d < 7; d++)
            if (advDowChecks[d]->isChecked()) rule.advDaysOfWeek.insert(d + 1);
    }
    return rule;
}

// 计算最多 count 条即将触发的时间
static QList<QDateTime> computePreviewTimes(const ScheduleRule& rule, int count = 20) {
    QList<QDateTime> times;
    if (rule.type == ScheduleRule::Periodic && rule.intervalSecs <= 0)
        return times;
    QDateTime cur = QDateTime::currentDateTime();
    for (int i = 0; i < count; ++i) {
        QDateTime next = calculateNextTrigger(rule, cur);
        if (!next.isValid()) break;
        times.append(next);
        cur = next;
    }
    return times;
}

// 更新预览列表和日历高亮
static void refreshPreview(
    QListWidget* previewList, QCalendarWidget* calendar, QLabel* ruleDescLabel,
    const ScheduleRule& rule, const QList<QDateTime>& extraTimes = {})
{
    previewList->clear();

    // 当前规则的预览
    QList<QDateTime> times = computePreviewTimes(rule, 20);

    // 合并额外时间（已有规则的触发时间）并去重排序
    QSet<qint64> seen;
    QList<QDateTime> allTimes;
    for (const QDateTime& t : times) {
        qint64 key = t.toSecsSinceEpoch();
        if (!seen.contains(key)) {
            seen.insert(key);
            allTimes.append(t);
        }
    }
    for (const QDateTime& t : extraTimes) {
        qint64 key = t.toSecsSinceEpoch();
        if (!seen.contains(key)) {
            seen.insert(key);
            allTimes.append(t);
        }
    }
    std::sort(allTimes.begin(), allTimes.end());

    // 规则描述
    QString desc;
    if (rule.type == ScheduleRule::Periodic)
        desc = formatRestartInterval(rule.intervalSecs);
    else if (rule.type == ScheduleRule::Advanced)
        desc = formatAdvancedRule(rule);
    else
        desc = formatDaysShort(rule.daysOfWeek) + u" "_s + rule.fixedTime.toString(u"HH:mm:ss"_s);
    ruleDescLabel->setText(desc);

    // 日历高亮
    QTextCharFormat normalFmt;
    QTextCharFormat highlightFmt;
    bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
    highlightFmt.setBackground(isDark ? QColor(33, 70, 111) : QColor(219, 234, 254));
    highlightFmt.setForeground(isDark ? QColor(96, 205, 255) : QColor(0, 95, 183));

    // 清除之前的高亮
    calendar->setDateTextFormat(QDate(), normalFmt);

    QSet<QDate> highlightDates;
    for (const QDateTime& t : allTimes) {
        highlightDates.insert(t.date());
        previewList->addItem(t.toString(u"yyyy-MM-dd HH:mm:ss"_s));
    }
    for (const QDate& d : highlightDates)
        calendar->setDateTextFormat(d, highlightFmt);

    if (!allTimes.isEmpty())
        calendar->setSelectedDate(allTimes.first().date());
}

bool showScheduleRuleEditDialog(QWidget* parent, const ScheduleRule* existing, ScheduleRule& outRule,
                                const QList<ScheduleRule>& otherRules) {
    QDialog dlg(parent, kDialogFlags);
    dlg.setWindowTitle(existing ? u"编辑规则"_s : u"添加规则"_s);
    dlg.setMinimumSize(720, 460);

    QHBoxLayout* mainLay = new QHBoxLayout(&dlg);

    // ===== 左侧：规则编辑 =====
    QWidget* leftWidget = new QWidget();
    QVBoxLayout* al = new QVBoxLayout(leftWidget);
    al->setContentsMargins(0, 0, 0, 0);

    al->addWidget(new QLabel(u"规则类型："_s));
    QHBoxLayout* typeBtnLay = new QHBoxLayout();
    QPushButton* periodicBtn = new QPushButton(u"周期重复"_s);
    QPushButton* fixedBtn = new QPushButton(u"固定时间"_s);
    QPushButton* advancedBtn = new QPushButton(u"高级"_s);
    for (auto* b : {periodicBtn, fixedBtn, advancedBtn}) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
    }
    bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
    QString checkedStyle = isDark
        ? u"QPushButton:checked { background-color: #21466f; color: #ffffff; border: 1px solid #60cdff; border-bottom: 2px solid #60cdff; }"_s
        : u"QPushButton:checked { background-color: #dbeafe; color: #1a1a1a; border: 1px solid #005fb7; border-bottom: 2px solid #005fb7; }"_s;
    for (auto* b : {periodicBtn, fixedBtn, advancedBtn}) {
        b->setStyleSheet(checkedStyle);
        typeBtnLay->addWidget(b);
    }
    al->addLayout(typeBtnLay);

    // 周期重复
    QWidget* periodicWidget = new QWidget();
    QHBoxLayout* pLay = new QHBoxLayout(periodicWidget);
    pLay->setContentsMargins(0, 4, 0, 0);
    QSpinBox* daySpin = new QSpinBox(); daySpin->setRange(0, 99999); daySpin->setSuffix(u" 天"_s);
    QSpinBox* hourSpin = new QSpinBox(); hourSpin->setRange(0, 23); hourSpin->setSuffix(u" 小时"_s);
    QSpinBox* minSpin = new QSpinBox(); minSpin->setRange(0, 59); minSpin->setSuffix(u" 分钟"_s);
    QSpinBox* secSpin = new QSpinBox(); secSpin->setRange(0, 59); secSpin->setSuffix(u" 秒"_s);
    pLay->addWidget(daySpin); pLay->addWidget(hourSpin); pLay->addWidget(minSpin); pLay->addWidget(secSpin);
    al->addWidget(periodicWidget);

    // 固定时间
    QWidget* fixedWidget = new QWidget();
    QVBoxLayout* fLay = new QVBoxLayout(fixedWidget);
    fLay->setContentsMargins(0, 4, 0, 0);
    fLay->addWidget(new QLabel(u"时间："_s));
    QTimeEdit* timeEdit = new QTimeEdit(QTime(0, 0, 0));
    timeEdit->setDisplayFormat("HH:mm:ss");
    fLay->addWidget(timeEdit);
    fLay->addWidget(new QLabel(u"星期（不选则每天）："_s));
    QHBoxLayout* dowLay = new QHBoxLayout();
    static constexpr QStringView dayNames[] = { u"一", u"二", u"三", u"四", u"五", u"六", u"日" };
    QCheckBox* dowChecks[7];
    for (int d = 0; d < 7; d++) {
        dowChecks[d] = new QCheckBox(dayNames[d].toString());
        dowLay->addWidget(dowChecks[d]);
    }
    fLay->addLayout(dowLay);
    fixedWidget->setVisible(false);
    al->addWidget(fixedWidget);

    // 高级规则
    QWidget* advWidget = new QWidget();
    QVBoxLayout* advLay = new QVBoxLayout(advWidget);
    advLay->setContentsMargins(0, 4, 0, 0);

    QHBoxLayout* advYearMonthLay = new QHBoxLayout();
    QCheckBox* advYearCheck = new QCheckBox(u"年"_s);
    QSpinBox* advYearSpin = new QSpinBox(); advYearSpin->setRange(2020, 2099); advYearSpin->setValue(QDate::currentDate().year());
    advYearSpin->setEnabled(false);
    advYearMonthLay->addWidget(advYearCheck); advYearMonthLay->addWidget(advYearSpin);
    QCheckBox* advMonthCheck = new QCheckBox(u"月"_s);
    QSpinBox* advMonthSpin = new QSpinBox(); advMonthSpin->setRange(1, 12); advMonthSpin->setSuffix(u" 月"_s);
    advMonthSpin->setEnabled(false);
    advYearMonthLay->addWidget(advMonthCheck); advYearMonthLay->addWidget(advMonthSpin);
    advLay->addLayout(advYearMonthLay);

    QHBoxLayout* advDayLay = new QHBoxLayout();
    QCheckBox* advDayCheck = new QCheckBox(u"日"_s);
    QSpinBox* advDaySpin = new QSpinBox(); advDaySpin->setRange(1, 31); advDaySpin->setSuffix(u" 日"_s);
    advDaySpin->setEnabled(false);
    advDayLay->addWidget(advDayCheck); advDayLay->addWidget(advDaySpin);
    advDayLay->addStretch();
    advLay->addLayout(advDayLay);

    advLay->addWidget(new QLabel(u"星期（不选则不限制）："_s));
    QHBoxLayout* advDowLay = new QHBoxLayout();
    QCheckBox* advDowChecks[7];
    for (int d = 0; d < 7; d++) {
        advDowChecks[d] = new QCheckBox(dayNames[d].toString());
        advDowLay->addWidget(advDowChecks[d]);
    }
    advLay->addLayout(advDowLay);

    QHBoxLayout* advTimeLay = new QHBoxLayout();
    QCheckBox* advHourCheck = new QCheckBox(u"小时"_s);
    QSpinBox* advHourSpin = new QSpinBox(); advHourSpin->setRange(0, 23); advHourSpin->setSuffix(u" 时"_s);
    advHourSpin->setEnabled(false);
    QCheckBox* advMinCheck = new QCheckBox(u"分钟"_s);
    advMinCheck->setChecked(true);
    QSpinBox* advMinSpin = new QSpinBox(); advMinSpin->setRange(0, 59); advMinSpin->setSuffix(u" 分"_s);
    QCheckBox* advSecCheck = new QCheckBox(u"秒"_s);
    QSpinBox* advSecSpin = new QSpinBox(); advSecSpin->setRange(0, 59); advSecSpin->setSuffix(u" 秒"_s);
    advSecSpin->setEnabled(false);
    advTimeLay->addWidget(advHourCheck); advTimeLay->addWidget(advHourSpin);
    advTimeLay->addWidget(advMinCheck); advTimeLay->addWidget(advMinSpin);
    advTimeLay->addWidget(advSecCheck); advTimeLay->addWidget(advSecSpin);
    advLay->addLayout(advTimeLay);

    QObject::connect(advYearCheck, &QCheckBox::toggled, advYearSpin, &QSpinBox::setEnabled);
    QObject::connect(advMonthCheck, &QCheckBox::toggled, advMonthSpin, &QSpinBox::setEnabled);
    QObject::connect(advDayCheck, &QCheckBox::toggled, advDaySpin, &QSpinBox::setEnabled);
    QObject::connect(advHourCheck, &QCheckBox::toggled, advHourSpin, &QSpinBox::setEnabled);
    QObject::connect(advMinCheck, &QCheckBox::toggled, advMinSpin, &QSpinBox::setEnabled);
    QObject::connect(advSecCheck, &QCheckBox::toggled, advSecSpin, &QSpinBox::setEnabled);

    advWidget->setVisible(false);
    al->addWidget(advWidget);

    auto switchType = [periodicWidget, fixedWidget, advWidget, periodicBtn, fixedBtn, advancedBtn](int idx) {
        periodicWidget->setVisible(idx == 0);
        fixedWidget->setVisible(idx == 1);
        advWidget->setVisible(idx == 2);
        periodicBtn->setChecked(idx == 0);
        fixedBtn->setChecked(idx == 1);
        advancedBtn->setChecked(idx == 2);
    };
    QObject::connect(periodicBtn, &QPushButton::clicked, [switchType]() { switchType(0); });
    QObject::connect(fixedBtn, &QPushButton::clicked, [switchType]() { switchType(1); });
    QObject::connect(advancedBtn, &QPushButton::clicked, [switchType]() { switchType(2); });
    switchType(0);

    // 编辑模式：预填已有规则
    if (existing) {
        if (existing->type == ScheduleRule::Periodic) {
            switchType(0);
            int total = existing->intervalSecs;
            daySpin->setValue(total / 86400);
            hourSpin->setValue((total % 86400) / 3600);
            minSpin->setValue((total % 3600) / 60);
            secSpin->setValue(total % 60);
        } else if (existing->type == ScheduleRule::FixedTime) {
            switchType(1);
            timeEdit->setTime(existing->fixedTime);
            for (int d = 0; d < 7; d++) {
                if (existing->daysOfWeek.contains(d + 1))
                    dowChecks[d]->setChecked(true);
            }
        } else if (existing->type == ScheduleRule::Advanced) {
            switchType(2);
            if (existing->advYear > 0) { advYearCheck->setChecked(true); advYearSpin->setValue(existing->advYear); }
            if (existing->advMonth > 0) { advMonthCheck->setChecked(true); advMonthSpin->setValue(existing->advMonth); }
            if (existing->advDay > 0) { advDayCheck->setChecked(true); advDaySpin->setValue(existing->advDay); }
            if (existing->advHour >= 0) { advHourCheck->setChecked(true); advHourSpin->setValue(existing->advHour); }
            if (existing->advMinute >= 0) { advMinCheck->setChecked(true); advMinSpin->setValue(existing->advMinute); }
            else { advMinCheck->setChecked(false); }
            if (existing->advSecond >= 0) { advSecCheck->setChecked(true); advSecSpin->setValue(existing->advSecond); }
            for (int d = 0; d < 7; d++) {
                if (existing->advDaysOfWeek.contains(d + 1))
                    advDowChecks[d]->setChecked(true);
            }
        }
    }

    al->addStretch();
    QHBoxLayout* abLay = new QHBoxLayout();
    abLay->addStretch();
    QPushButton* okB = new QPushButton(u"确定"_s);
    QPushButton* cancelB = new QPushButton(u"取消"_s);
    QObject::connect(okB, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelB, &QPushButton::clicked, &dlg, &QDialog::reject);
    abLay->addWidget(okB); abLay->addWidget(cancelB); abLay->addStretch();
    al->addLayout(abLay);

    mainLay->addWidget(leftWidget, 1);

    // ===== 右侧：计划预览 =====
    QWidget* previewWidget = new QWidget();
    QVBoxLayout* prevLay = new QVBoxLayout(previewWidget);
    prevLay->setContentsMargins(8, 0, 0, 0);

    QLabel* prevTitle = new QLabel(u"计划预览"_s);
    QFont titleFont = prevTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    prevTitle->setFont(titleFont);
    prevLay->addWidget(prevTitle);

    QLabel* ruleDescLabel = new QLabel(u"-"_s);
    ruleDescLabel->setWordWrap(true);
    ruleDescLabel->setStyleSheet(isDark
        ? u"color: #aaaaaa; padding: 2px 0;"_s
        : u"color: #666666; padding: 2px 0;"_s);
    prevLay->addWidget(ruleDescLabel);

    QCalendarWidget* calendar = new QCalendarWidget();
    calendar->setGridVisible(true);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setFixedHeight(200);
    calendar->setNavigationBarVisible(true);
    prevLay->addWidget(calendar);

    QLabel* tzLabel = new QLabel(u"系统时区：%1"_s.arg(
        QString::fromUtf8(QTimeZone::systemTimeZone().id())));
    tzLabel->setStyleSheet(isDark
        ? u"color: #888888; font-size: 11px;"_s
        : u"color: #999999; font-size: 11px;"_s);
    prevLay->addWidget(tzLabel);

    QListWidget* previewList = new QListWidget();
    previewList->setAlternatingRowColors(true);
    prevLay->addWidget(previewList, 1);

    mainLay->addWidget(previewWidget, 1);

    // 预计算其他规则的触发时间用于合并预览
    QList<QDateTime> otherTimes;
    for (const ScheduleRule& r : otherRules) {
        QList<QDateTime> t = computePreviewTimes(r, 10);
        otherTimes.append(t);
    }

    // 刷新预览的 lambda
    auto doRefreshPreview = [&]() {
        ScheduleRule rule = buildRuleFromUI(
            periodicBtn, fixedBtn,
            daySpin, hourSpin, minSpin, secSpin,
            timeEdit, dowChecks,
            advYearCheck, advYearSpin, advMonthCheck, advMonthSpin,
            advDayCheck, advDaySpin, advDowChecks,
            advHourCheck, advHourSpin, advMinCheck, advMinSpin,
            advSecCheck, advSecSpin);
        refreshPreview(previewList, calendar, ruleDescLabel, rule, otherTimes);
    };

    // 连接所有控件的变化信号到预览刷新
    auto connectRefresh = [&](QSpinBox* sb) {
        QObject::connect(sb, &QSpinBox::valueChanged, &dlg, doRefreshPreview);
    };
    auto connectCheckRefresh = [&](QCheckBox* cb) {
        QObject::connect(cb, &QCheckBox::toggled, &dlg, doRefreshPreview);
    };
    connectRefresh(daySpin); connectRefresh(hourSpin); connectRefresh(minSpin); connectRefresh(secSpin);
    QObject::connect(timeEdit, &QTimeEdit::timeChanged, &dlg, doRefreshPreview);
    for (int d = 0; d < 7; d++) {
        connectCheckRefresh(dowChecks[d]);
        connectCheckRefresh(advDowChecks[d]);
    }
    connectCheckRefresh(advYearCheck); connectRefresh(advYearSpin);
    connectCheckRefresh(advMonthCheck); connectRefresh(advMonthSpin);
    connectCheckRefresh(advDayCheck); connectRefresh(advDaySpin);
    connectCheckRefresh(advHourCheck); connectRefresh(advHourSpin);
    connectCheckRefresh(advMinCheck); connectRefresh(advMinSpin);
    connectCheckRefresh(advSecCheck); connectRefresh(advSecSpin);
    QObject::connect(periodicBtn, &QPushButton::clicked, &dlg, doRefreshPreview);
    QObject::connect(fixedBtn, &QPushButton::clicked, &dlg, doRefreshPreview);
    QObject::connect(advancedBtn, &QPushButton::clicked, &dlg, doRefreshPreview);

    // 初始刷新
    doRefreshPreview();

    while (true) {
        if (dlg.exec() != QDialog::Accepted) return false;

        ScheduleRule rule = buildRuleFromUI(
            periodicBtn, fixedBtn,
            daySpin, hourSpin, minSpin, secSpin,
            timeEdit, dowChecks,
            advYearCheck, advYearSpin, advMonthCheck, advMonthSpin,
            advDayCheck, advDaySpin, advDowChecks,
            advHourCheck, advHourSpin, advMinCheck, advMinSpin,
            advSecCheck, advSecSpin);

        if (rule.type == ScheduleRule::Periodic) {
            if (rule.intervalSecs <= 0) {
                showMessageDialog(&dlg, u"提示"_s, u"周期不能为 0。"_s);
                continue;
            }
        } else if (rule.type == ScheduleRule::Advanced) {
            QString err;
            QDate today = QDate::currentDate();
            if (rule.advYear > 0 && rule.advYear < today.year()) {
                err = u"指定的年份 %1 已过期。"_s.arg(rule.advYear);
            } else if (rule.advYear > 0 && rule.advMonth > 0) {
                QDate endOfMonth(rule.advYear, rule.advMonth, QDate(rule.advYear, rule.advMonth, 1).daysInMonth());
                if (endOfMonth < today)
                    err = u"指定的 %1年%2月 已过期。"_s.arg(rule.advYear).arg(rule.advMonth);
            }
            if (err.isEmpty() && rule.advMonth > 0 && rule.advDay > 0) {
                int refYear = (rule.advYear > 0) ? rule.advYear : today.year();
                int maxDays = QDate(refYear, rule.advMonth, 1).daysInMonth();
                if (rule.advDay > maxDays)
                    err = u"%1月最多只有 %2 天，无法指定第 %3 天。"_s.arg(rule.advMonth).arg(maxDays).arg(rule.advDay);
            }
            if (err.isEmpty() && rule.advHour < 0 && rule.advMinute < 0 && rule.advSecond < 0
                && rule.advDay < 0 && rule.advMonth < 0 && rule.advYear < 0
                && rule.advDaysOfWeek.isEmpty()) {
                err = u"所有条件均未指定，规则将每秒触发。请至少设置一个时间条件。"_s;
            }
            if (err.isEmpty()) {
                QDateTime next = calculateNextTrigger(rule);
                if (!next.isValid())
                    err = u"无法计算下一次触发时间，请检查规则设置是否合理。"_s;
            }
            if (!err.isEmpty()) {
                showMessageDialog(&dlg, u"规则异常"_s, err);
                continue;
            }
        }
        rule.nextTrigger = calculateNextTrigger(rule);
        outRule = rule;
        return true;
    }
}
