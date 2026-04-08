#include "ScheduleRuleEditor.h"
#include "DialogHelpers.h"
#include <QtWidgets>

using namespace Qt::Literals::StringLiterals;

bool showScheduleRuleEditDialog(QWidget* parent, const ScheduleRule* existing, ScheduleRule& outRule) {
    QDialog dlg(parent, kDialogFlags);
    dlg.setWindowTitle(existing ? u"编辑规则"_s : u"添加规则"_s);
    dlg.setFixedWidth(400);
    dlg.setMinimumHeight(320);
    QVBoxLayout* al = new QVBoxLayout(&dlg);

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
    pLay->addWidget(daySpin); pLay->addWidget(hourSpin); pLay->addWidget(minSpin);
    al->addWidget(periodicWidget);

    // 固定时间
    QWidget* fixedWidget = new QWidget();
    QVBoxLayout* fLay = new QVBoxLayout(fixedWidget);
    fLay->setContentsMargins(0, 4, 0, 0);
    fLay->addWidget(new QLabel(u"时间："_s));
    QTimeEdit* timeEdit = new QTimeEdit(QTime(0, 0));
    timeEdit->setDisplayFormat("HH:mm");
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
    QCheckBox* advYearCheck = new QCheckBox(u"指定年份"_s);
    QSpinBox* advYearSpin = new QSpinBox(); advYearSpin->setRange(2020, 2099); advYearSpin->setValue(QDate::currentDate().year());
    advYearSpin->setEnabled(false);
    advYearMonthLay->addWidget(advYearCheck); advYearMonthLay->addWidget(advYearSpin);
    QCheckBox* advMonthCheck = new QCheckBox(u"指定月份"_s);
    QSpinBox* advMonthSpin = new QSpinBox(); advMonthSpin->setRange(1, 12); advMonthSpin->setSuffix(u" 月"_s);
    advMonthSpin->setEnabled(false);
    advYearMonthLay->addWidget(advMonthCheck); advYearMonthLay->addWidget(advMonthSpin);
    advLay->addLayout(advYearMonthLay);

    QHBoxLayout* advDayLay = new QHBoxLayout();
    QCheckBox* advDayCheck = new QCheckBox(u"指定日期"_s);
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
    QCheckBox* advHourCheck = new QCheckBox(u"指定小时"_s);
    QSpinBox* advHourSpin = new QSpinBox(); advHourSpin->setRange(0, 23); advHourSpin->setSuffix(u" 时"_s);
    advHourSpin->setEnabled(false);
    QCheckBox* advMinCheck = new QCheckBox(u"指定分钟"_s);
    advMinCheck->setChecked(true);
    QSpinBox* advMinSpin = new QSpinBox(); advMinSpin->setRange(0, 59); advMinSpin->setSuffix(u" 分"_s);
    advTimeLay->addWidget(advHourCheck); advTimeLay->addWidget(advHourSpin);
    advTimeLay->addWidget(advMinCheck); advTimeLay->addWidget(advMinSpin);
    advLay->addLayout(advTimeLay);

    QObject::connect(advYearCheck, &QCheckBox::toggled, advYearSpin, &QSpinBox::setEnabled);
    QObject::connect(advMonthCheck, &QCheckBox::toggled, advMonthSpin, &QSpinBox::setEnabled);
    QObject::connect(advDayCheck, &QCheckBox::toggled, advDaySpin, &QSpinBox::setEnabled);
    QObject::connect(advHourCheck, &QCheckBox::toggled, advHourSpin, &QSpinBox::setEnabled);
    QObject::connect(advMinCheck, &QCheckBox::toggled, advMinSpin, &QSpinBox::setEnabled);

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

    while (true) {
        if (dlg.exec() != QDialog::Accepted) return false;

        ScheduleRule rule;
        if (periodicBtn->isChecked()) {
            rule.type = ScheduleRule::Periodic;
            rule.intervalSecs = daySpin->value() * 86400 + hourSpin->value() * 3600 + minSpin->value() * 60;
            if (rule.intervalSecs <= 0) {
                showMessageDialog(&dlg, u"提示"_s, u"周期不能为 0。"_s);
                continue;
            }
        } else if (fixedBtn->isChecked()) {
            rule.type = ScheduleRule::FixedTime;
            rule.fixedTime = timeEdit->time();
            for (int d = 0; d < 7; d++) {
                if (dowChecks[d]->isChecked()) rule.daysOfWeek.insert(d + 1);
            }
        } else {
            rule.type = ScheduleRule::Advanced;
            rule.advMinute = advMinCheck->isChecked() ? advMinSpin->value() : -1;
            rule.advHour = advHourCheck->isChecked() ? advHourSpin->value() : -1;
            rule.advDay = advDayCheck->isChecked() ? advDaySpin->value() : -1;
            rule.advMonth = advMonthCheck->isChecked() ? advMonthSpin->value() : -1;
            rule.advYear = advYearCheck->isChecked() ? advYearSpin->value() : -1;
            for (int d = 0; d < 7; d++) {
                if (advDowChecks[d]->isChecked()) rule.advDaysOfWeek.insert(d + 1);
            }
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
            if (err.isEmpty() && rule.advHour < 0 && rule.advMinute < 0
                && rule.advDay < 0 && rule.advMonth < 0 && rule.advYear < 0
                && rule.advDaysOfWeek.isEmpty()) {
                err = u"所有条件均未指定，规则将每分钟触发。请至少设置一个时间条件。"_s;
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
