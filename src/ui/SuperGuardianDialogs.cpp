#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include <QtWidgets>

// ---- 定时规则设置对话框（多规则，支持周期和固定时间） ----

void SuperGuardian::contextSetScheduleRules(const QList<int>& rows, bool forRun) {
    QList<ScheduleRule> initRules;
    if (rows.size() == 1) {
        int itemIdx = findItemIndexById(rowId(rows[0]));
        if (itemIdx >= 0)
            initRules = forRun ? items[itemIdx].runRules : items[itemIdx].restartRules;
    }

    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(forRun ? u"定时运行规则"_s : u"定时重启规则"_s);
    dlg.setMinimumSize(420, 380);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"规则列表（可添加多个，时间重复时只执行一次）："_s));

    QListWidget* ruleList = new QListWidget();
    ruleList->setContextMenuPolicy(Qt::CustomContextMenu);
    ruleList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ruleList->setDragDropMode(QAbstractItemView::InternalMove);
    ruleList->setDefaultDropAction(Qt::MoveAction);
    ruleList->setSpacing(0);
    lay->addWidget(ruleList, 1);

    auto formatRule = [](const ScheduleRule& r) -> QString {
        if (r.type == ScheduleRule::Periodic) return formatRestartInterval(r.intervalSecs);
        if (r.type == ScheduleRule::Advanced) return formatAdvancedRule(r);
        return formatDaysShort(r.daysOfWeek) + " " + r.fixedTime.toString("HH:mm");
    };

    QList<ScheduleRule>* editRules = new QList<ScheduleRule>(initRules);
    auto refreshList = [&]() {
        ruleList->clear();
        for (int i = 0; i < editRules->size(); i++) {
            QListWidgetItem* item = new QListWidgetItem(formatRule((*editRules)[i]));
            item->setData(Qt::UserRole, i);
            ruleList->addItem(item);
        }
    };
    auto syncRulesFromList = [&]() {
        QList<ScheduleRule> reordered;
        for (int i = 0; i < ruleList->count(); i++) {
            int origIdx = ruleList->item(i)->data(Qt::UserRole).toInt();
            if (origIdx >= 0 && origIdx < editRules->size())
                reordered.append((*editRules)[origIdx]);
        }
        *editRules = reordered;
        for (int i = 0; i < ruleList->count(); i++)
            ruleList->item(i)->setData(Qt::UserRole, i);
    };
    refreshList();

    QObject::connect(ruleList, &QListWidget::customContextMenuRequested, [&](const QPoint& pos) {
        QMenu ctxMenu(ruleList);
        QList<QListWidgetItem*> selected = ruleList->selectedItems();
        if (!selected.isEmpty()) {
            ctxMenu.addAction(u"复制规则"_s, [&]() {
                syncRulesFromList();
                copiedScheduleRules.clear();
                QList<QListWidgetItem*> sel = ruleList->selectedItems();
                for (auto* s : sel) {
                    int idx = ruleList->row(s);
                    if (idx >= 0 && idx < editRules->size())
                        copiedScheduleRules.append((*editRules)[idx]);
                }
                if (!copiedScheduleRules.isEmpty())
                    copiedRulesTime = QDateTime::currentDateTime();
            });
        }
        bool canPaste = !copiedScheduleRules.isEmpty() && copiedRulesTime.isValid()
            && copiedRulesTime.secsTo(QDateTime::currentDateTime()) < 7200;
        QAction* pasteAct = ctxMenu.addAction(u"粘贴规则"_s, [&]() {
            if (copiedScheduleRules.isEmpty()) return;
            if (!copiedRulesTime.isValid() || copiedRulesTime.secsTo(QDateTime::currentDateTime()) >= 7200) {
                copiedScheduleRules.clear();
                return;
            }
            syncRulesFromList();
            for (const ScheduleRule& r : copiedScheduleRules) {
                ScheduleRule nr = r;
                nr.nextTrigger = calculateNextTrigger(nr);
                editRules->append(nr);
            }
            refreshList();
        });
        pasteAct->setEnabled(canPaste);
        ctxMenu.exec(ruleList->viewport()->mapToGlobal(pos));
    });

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton(u"添加"_s);
    QPushButton* removeBtn = new QPushButton(u"删除"_s);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    // editIndex < 0 表示添加新规则，>= 0 表示编辑已有规则
    auto openRuleDialog = [&](int editIndex) {
        const ScheduleRule* existing = (editIndex >= 0 && editIndex < editRules->size()) ? &(*editRules)[editIndex] : nullptr;
        QDialog addDlg(&dlg, kDialogFlags);
        addDlg.setWindowTitle(existing ? u"编辑规则"_s : u"添加规则"_s);
        addDlg.setFixedWidth(400);
        addDlg.setMinimumHeight(320);
        QVBoxLayout* al = new QVBoxLayout(&addDlg);

        // 3个水平按钮选择规则类型
        al->addWidget(new QLabel(u"规则类型："_s));
        QHBoxLayout* typeBtnLay = new QHBoxLayout();
        QPushButton* periodicBtn = new QPushButton(u"周期重复"_s);
        QPushButton* fixedBtn = new QPushButton(u"固定时间"_s);
        QPushButton* advancedBtn = new QPushButton(u"高级"_s);
        periodicBtn->setCheckable(true);
        fixedBtn->setCheckable(true);
        advancedBtn->setCheckable(true);
        periodicBtn->setAutoExclusive(true);
        fixedBtn->setAutoExclusive(true);
        advancedBtn->setAutoExclusive(true);
        bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
        QString checkedStyle = isDark
            ? u"QPushButton:checked { background-color: #21466f; color: #ffffff; border: 1px solid #60cdff; border-bottom: 2px solid #60cdff; }"_s
            : u"QPushButton:checked { background-color: #dbeafe; color: #1a1a1a; border: 1px solid #005fb7; border-bottom: 2px solid #005fb7; }"_s;
        periodicBtn->setStyleSheet(checkedStyle);
        fixedBtn->setStyleSheet(checkedStyle);
        advancedBtn->setStyleSheet(checkedStyle);
        typeBtnLay->addWidget(periodicBtn);
        typeBtnLay->addWidget(fixedBtn);
        typeBtnLay->addWidget(advancedBtn);
        al->addLayout(typeBtnLay);

        // 周期重复设置界面
        QWidget* periodicWidget = new QWidget();
        QHBoxLayout* pLay = new QHBoxLayout(periodicWidget);
        pLay->setContentsMargins(0, 4, 0, 0);
        QSpinBox* daySpin = new QSpinBox(); daySpin->setRange(0, 99999); daySpin->setSuffix(u" 天"_s);
        QSpinBox* hourSpin = new QSpinBox(); hourSpin->setRange(0, 23); hourSpin->setValue(0); hourSpin->setSuffix(u" 小时"_s);
        QSpinBox* minSpin = new QSpinBox(); minSpin->setRange(0, 59); minSpin->setSuffix(u" 分钟"_s);
        pLay->addWidget(daySpin); pLay->addWidget(hourSpin); pLay->addWidget(minSpin);
        al->addWidget(periodicWidget);

        // 固定时间设置界面
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

        // 高级规则设置界面
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

        // 类型切换逻辑
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

        // 默认显示周期重复
        switchType(0);

        // 编辑模式：预填已有规则的值
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
        QObject::connect(okB, &QPushButton::clicked, &addDlg, &QDialog::accept);
        QObject::connect(cancelB, &QPushButton::clicked, &addDlg, &QDialog::reject);
        abLay->addWidget(okB); abLay->addWidget(cancelB); abLay->addStretch();
        al->addLayout(abLay);

        while (true) {
            if (addDlg.exec() != QDialog::Accepted) return;

            ScheduleRule rule;
            if (periodicBtn->isChecked()) {
                rule.type = ScheduleRule::Periodic;
                rule.intervalSecs = daySpin->value() * 86400 + hourSpin->value() * 3600 + minSpin->value() * 60;
                if (rule.intervalSecs <= 0) {
                    showMessageDialog(&addDlg, u"提示"_s, u"周期不能为 0。"_s);
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
                // 高级规则验证
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
                    showMessageDialog(&addDlg, u"规则异常"_s, err);
                    continue;
                }
            }
            rule.nextTrigger = calculateNextTrigger(rule);
            if (editIndex >= 0 && editIndex < editRules->size()) {
                (*editRules)[editIndex] = rule;
            } else {
                editRules->append(rule);
            }
            refreshList();
            break;
        }
    };

    QObject::connect(addBtn, &QPushButton::clicked, [&]() { syncRulesFromList(); openRuleDialog(-1); });

    QObject::connect(ruleList, &QListWidget::itemDoubleClicked, [&](QListWidgetItem*) {
        syncRulesFromList();
        int ci = ruleList->currentRow();
        if (ci >= 0 && ci < editRules->size())
            openRuleDialog(ci);
    });

    QObject::connect(removeBtn, &QPushButton::clicked, [&]() {
        QList<QListWidgetItem*> selected = ruleList->selectedItems();
        if (selected.isEmpty()) return;
        QString msg = selected.size() == 1
            ? u"确认删除选中的规则吗？"_s
            : u"确认删除选中的 %1 条规则吗？"_s.arg(selected.size());
        if (!showMessageDialog(&dlg, u"删除规则"_s, msg, true)) return;
        syncRulesFromList();
        QList<int> indices;
        for (auto* item : selected)
            indices << ruleList->row(item);
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int idx : indices)
            editRules->removeAt(idx);
        refreshList();
    });

    // 定时运行模式下添加选项复选框
    QCheckBox* trackDurationCheck = nullptr;
    QCheckBox* hideWindowCheck = nullptr;
    if (forRun) {
        hideWindowCheck = new QCheckBox(u"隐藏运行窗口"_s);
        hideWindowCheck->setToolTip(u"启动程序时隐藏其窗口，适用于命令行脚本等不需要显示窗口的程序"_s);
        if (rows.size() == 1) {
            int itemIdx = findItemIndexById(rowId(rows[0]));
            if (itemIdx >= 0)
                hideWindowCheck->setChecked(items[itemIdx].runHideWindow);
        }
        lay->addWidget(hideWindowCheck);

        trackDurationCheck = new QCheckBox(u"监控持续运行时长"_s);
        trackDurationCheck->setToolTip(u"选中时在程序列表中显示“持续运行时长”，未选中则始终显示“-”"_s);
        if (rows.size() == 1) {
            int itemIdx = findItemIndexById(rowId(rows[0]));
            if (itemIdx >= 0)
                trackDurationCheck->setChecked(items[itemIdx].trackRunDuration);
        }
        lay->addWidget(trackDurationCheck);
    }

    QHBoxLayout* dlgBtnLay = new QHBoxLayout();
    dlgBtnLay->addStretch();
    QPushButton* clearBtn = new QPushButton(u"清空"_s);
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(clearBtn, &QPushButton::clicked, [&]() {
        if (editRules->isEmpty()) return;
        if (!showMessageDialog(&dlg, u"清空规则"_s, u"确认清空所有规则吗？"_s, true)) return;
        editRules->clear();
        refreshList();
    });
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlgBtnLay->addWidget(clearBtn);
    dlgBtnLay->addWidget(okBtn);
    dlgBtnLay->addWidget(cancelBtn);
    dlgBtnLay->addStretch();
    lay->addLayout(dlgBtnLay);

    if (dlg.exec() != QDialog::Accepted) { delete editRules; return; }

    syncRulesFromList();
    QList<ScheduleRule> finalRules = *editRules;
    delete editRules;

    for (int row : rows) {
        int itemIdx = findItemIndexById(rowId(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        if (forRun) {
            item.runRules = finalRules;
            if (trackDurationCheck)
                item.trackRunDuration = trackDurationCheck->isChecked();
            if (hideWindowCheck)
                item.runHideWindow = hideWindowCheck->isChecked();
        } else {
            item.restartRules = finalRules;
        }
        auto setCell = [&](int col, const QString& text) {
            if (tableWidget->item(row, col)) {
                tableWidget->item(row, col)->setText(text);
                tableWidget->item(row, col)->setToolTip(text);
            }
        };
        if (item.scheduledRunEnabled) {
            setCell(7, formatScheduleRules(item.runRules));
            QDateTime nt = nextTriggerTime(item.runRules);
            setCell(8, nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
        } else if (item.restartRulesActive) {
            setCell(7, formatScheduleRules(item.restartRules));
            QDateTime nt = nextTriggerTime(item.restartRules);
            setCell(8, nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
        } else if (forRun && !item.runRules.isEmpty()) {
            setCell(7, formatScheduleRules(item.runRules));
            setCell(8, "-");
        } else if (!forRun && !item.restartRules.isEmpty()) {
            setCell(7, formatScheduleRules(item.restartRules));
            setCell(8, "-");
        } else {
            setCell(7, u"-"_s);
            setCell(8, "-");
        }
        setCell(9, item.scheduledRunEnabled ? "-" : formatStartDelay(item.startDelaySecs));
        updateButtonStates(row);
        logOperation(forRun ? u"设置定时运行规则"_s : u"设置定时重启规则"_s, programId(item.processName, item.launchArgs));
    }
    saveSettings();
}
