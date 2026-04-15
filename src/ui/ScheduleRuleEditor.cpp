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

// 更新月度预览面板（仿 TrueNAS SCALE 风格，按月显示触发时间）
static void refreshMonthPreview(
    QListWidget* previewList, QCalendarWidget* calendar,
    QLabel* ruleDescLabel, QLabel* summaryLabel,
    const ScheduleRule& rule, const QDate& filterDate = QDate())
{
    previewList->clear();
    int year = calendar->yearShown();
    int month = calendar->monthShown();

    // 规则描述
    QString desc;
    if (rule.type == ScheduleRule::Periodic)
        desc = formatRestartInterval(rule.intervalSecs);
    else if (rule.type == ScheduleRule::Advanced)
        desc = formatAdvancedRule(rule);
    else
        desc = formatDaysShort(rule.daysOfWeek) + u" "_s + rule.fixedTime.toString(u"HH:mm:ss"_s);
    ruleDescLabel->setText(desc);

    // 高级规则未指定任何条件时，直接显示提示
    if (rule.type == ScheduleRule::Advanced
        && rule.advHour < 0 && rule.advMinute < 0 && rule.advSecond < 0
        && rule.advDay < 0 && rule.advMonth < 0 && rule.advYear < 0
        && rule.advDaysOfWeek.isEmpty()) {
        bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
        calendar->setDateTextFormat(QDate(), QTextCharFormat());
        summaryLabel->setText(u"请至少勾选一个时间条件"_s);
        return;
    }

    // 计算完整触发次数和触发日期
    int totalCount = countTriggersInMonth(rule, year, month);
    QSet<QDate> highlightDates = triggerDatesInMonth(rule, year, month);

    // 计算用于列表显示的触发时间（最多25条）
    QList<QDateTime> displayTimes = computeTriggersInMonth(rule, year, month, 25);

    // 日历高亮
    bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
    QTextCharFormat normalFmt;
    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(isDark ? QColor(33, 70, 111) : QColor(219, 234, 254));
    highlightFmt.setForeground(isDark ? QColor(96, 205, 255) : QColor(0, 95, 183));
    calendar->setDateTextFormat(QDate(), normalFmt);
    for (const QDate& d : highlightDates)
        calendar->setDateTextFormat(d, highlightFmt);

    // 摘要信息
    static constexpr QStringView dowNames[] = { u"", u"周一", u"周二", u"周三", u"周四", u"周五", u"周六", u"周日" };
    if (filterDate.isValid() && filterDate.year() == year && filterDate.month() == month) {
        int dayCount = 0;
        for (const QDateTime& t : displayTimes)
            if (t.date() == filterDate) dayCount++;
        // 如果列表截断导致dayCount不准确，使用单独计算
        if (totalCount > 25) {
            ScheduleRule tmp = rule;
            // 粗略估算：使用triggerDatesInMonth检查该日期是否有触发
            dayCount = highlightDates.contains(filterDate) ? qMax(dayCount, 1) : 0;
        }
        summaryLabel->setText(u"%1年%2月%3日 %4\n点击其他日期或切换月份查看全月"_s
            .arg(filterDate.year()).arg(filterDate.month()).arg(filterDate.day())
            .arg(dowNames[filterDate.dayOfWeek()]));
    } else {
        summaryLabel->setText(u"本月 %1 次触发"_s.arg(totalCount));
    }

    // 填充触发时间列表
    if (filterDate.isValid() && filterDate.year() == year && filterDate.month() == month) {
        // 过滤模式：计算该日期的触发时间
        // 需要重新计算以获取该日的所有触发（最多25条）
        QList<QDateTime> allMonth = computeTriggersInMonth(rule, year, month, 500);
        int shown = 0;
        for (const QDateTime& t : allMonth) {
            if (t.date() == filterDate) {
                previewList->addItem(t.toString(u"HH:mm:ss"_s));
                if (++shown >= 25) break;
            }
        }
    } else {
        // 全月模式：按日期分组显示（最多25条触发时间）
        QDate lastDate;
        int shown = 0;
        for (const QDateTime& t : displayTimes) {
            if (shown >= 25) break;
            if (t.date() != lastDate) {
                lastDate = t.date();
                QString headerText = lastDate.toString(u"yyyy-MM-dd"_s) + u" "_s
                    + dowNames[lastDate.dayOfWeek()].toString();
                QListWidgetItem* headerItem = new QListWidgetItem(headerText);
                QFont hdrFont = previewList->font();
                hdrFont.setBold(true);
                headerItem->setFont(hdrFont);
                headerItem->setFlags(headerItem->flags() & ~Qt::ItemIsSelectable);
                headerItem->setBackground(isDark ? QColor(40, 40, 45) : QColor(240, 240, 245));
                previewList->addItem(headerItem);
            }
            previewList->addItem(u"    %1"_s.arg(t.toString(u"HH:mm:ss"_s)));
            shown++;
        }
        if (totalCount > 25) {
            QListWidgetItem* moreItem = new QListWidgetItem(
                u"    … 还有 %1 条"_s.arg(totalCount - 25));
            moreItem->setFlags(moreItem->flags() & ~Qt::ItemIsSelectable);
            moreItem->setForeground(isDark ? QColor(150, 150, 150) : QColor(120, 120, 120));
            previewList->addItem(moreItem);
        }
    }
}

bool showScheduleRuleEditDialog(QWidget* parent, const ScheduleRule* existing, ScheduleRule& outRule) {
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
    advLay->setSpacing(4);

    auto addSectionTitle = [&](const QString& title) {
        QLabel* lbl = new QLabel(title);
        QFont f = lbl->font(); f.setBold(true); lbl->setFont(f);
        advLay->addWidget(lbl);
        QFrame* line = new QFrame();
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        advLay->addWidget(line);
    };

    // ── 时间 ──
    addSectionTitle(u"时间"_s);
    auto makeTimeRow = [&](const QString& label, int lo, int hi, const QString& suffix) {
        QHBoxLayout* row = new QHBoxLayout();
        QCheckBox* cb = new QCheckBox(label);
        QSpinBox* sb = new QSpinBox(); sb->setRange(lo, hi); sb->setSuffix(suffix); sb->setEnabled(false);
        row->addWidget(cb); row->addWidget(sb, 1);
        advLay->addLayout(row);
        QObject::connect(cb, &QCheckBox::toggled, sb, &QSpinBox::setEnabled);
        return std::make_pair(cb, sb);
    };
    auto [advHourCheck, advHourSpin] = makeTimeRow(u"小时"_s, 0, 23, u" 时"_s);
    auto [advMinCheck, advMinSpin] = makeTimeRow(u"分钟"_s, 0, 59, u" 分"_s);
    auto [advSecCheck, advSecSpin] = makeTimeRow(u"秒"_s, 0, 59, u" 秒"_s);

    // ── 日期 ──
    addSectionTitle(u"日期"_s);
    auto [advYearCheck, advYearSpin] = makeTimeRow(u"年"_s, 2020, 2099, u""_s);
    advYearSpin->setValue(QDate::currentDate().year());
    auto [advMonthCheck, advMonthSpin] = makeTimeRow(u"月"_s, 1, 12, u" 月"_s);
    auto [advDayCheck, advDaySpin] = makeTimeRow(u"日"_s, 1, 31, u" 日"_s);

    // ── 星期几 ──
    addSectionTitle(u"星期几"_s);
    static constexpr QStringView advDowNames[] = { u"周一", u"周二", u"周三", u"周四", u"周五", u"周六", u"周日" };
    QGridLayout* advDowGrid = new QGridLayout();
    advDowGrid->setSpacing(2);
    QCheckBox* advDowChecks[7];
    for (int d = 0; d < 7; d++) {
        advDowChecks[d] = new QCheckBox(advDowNames[d].toString());
        advDowGrid->addWidget(advDowChecks[d], d / 4, d % 4);
    }
    advLay->addLayout(advDowGrid);

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

    CalendarWithNav calNav = createCalendarWithNav(isDark);
    QCalendarWidget* calendar = calNav.calendar;
    prevLay->addWidget(calNav.widget);

    QLabel* tzLabel = new QLabel(u"系统时区：%1"_s.arg(
        QString::fromUtf8(QTimeZone::systemTimeZone().id())));
    tzLabel->setStyleSheet(isDark
        ? u"color: #888888; font-size: 11px;"_s
        : u"color: #999999; font-size: 11px;"_s);
    prevLay->addWidget(tzLabel);

    QLabel* summaryLabel = new QLabel(u"-"_s);
    summaryLabel->setWordWrap(true);
    summaryLabel->setStyleSheet(isDark
        ? u"color: #cccccc; padding: 2px 0;"_s
        : u"color: #333333; padding: 2px 0;"_s);
    prevLay->addWidget(summaryLabel);

    QListWidget* previewList = new QListWidget();
    previewList->setAlternatingRowColors(true);
    prevLay->addWidget(previewList, 1);

    previewWidget->setFixedWidth(280);
    mainLay->addWidget(previewWidget, 0);

    // 日期过滤状态（用于点击日历高亮日期查看当日详情）
    QDate previewFilterDate;

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
        refreshMonthPreview(previewList, calendar, ruleDescLabel, summaryLabel, rule, previewFilterDate);
    };

    // 规则控件变化时清除日期过滤并刷新
    auto onRuleChanged = [&]() {
        previewFilterDate = QDate();
        doRefreshPreview();
    };

    // 日历月份导航切换 → 清除过滤并重新计算该月预览
    QObject::connect(calendar, &QCalendarWidget::currentPageChanged, &dlg, [&](int, int) {
        previewFilterDate = QDate();
        doRefreshPreview();
    });

    // 日历日期点击 → 切换日期过滤（再次点击同一日期取消过滤）
    QObject::connect(calendar, &QCalendarWidget::clicked, &dlg, [&](const QDate& date) {
        previewFilterDate = (previewFilterDate == date) ? QDate() : date;
        doRefreshPreview();
    });

    // 连接所有控件的变化信号到预览刷新
    auto connectRefresh = [&](QSpinBox* sb) {
        QObject::connect(sb, &QSpinBox::valueChanged, &dlg, onRuleChanged);
    };
    auto connectCheckRefresh = [&](QCheckBox* cb) {
        QObject::connect(cb, &QCheckBox::toggled, &dlg, onRuleChanged);
    };
    connectRefresh(daySpin); connectRefresh(hourSpin); connectRefresh(minSpin); connectRefresh(secSpin);
    QObject::connect(timeEdit, &QTimeEdit::timeChanged, &dlg, onRuleChanged);
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
    QObject::connect(periodicBtn, &QPushButton::clicked, &dlg, onRuleChanged);
    QObject::connect(fixedBtn, &QPushButton::clicked, &dlg, onRuleChanged);
    QObject::connect(advancedBtn, &QPushButton::clicked, &dlg, onRuleChanged);

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
