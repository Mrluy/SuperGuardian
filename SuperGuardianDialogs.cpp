#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include <QtWidgets>

// ---- 定时规则设置对话框（多规则，支持周期和固定时间） ----

void SuperGuardian::contextSetScheduleRules(const QList<int>& rows, bool forRun) {
    QList<ScheduleRule> initRules;
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
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
    lay->addWidget(ruleList);

    auto formatRule = [](const ScheduleRule& r) -> QString {
        if (r.type == ScheduleRule::Periodic) return formatRestartInterval(r.intervalSecs);
        return formatDaysShort(r.daysOfWeek) + " " + r.fixedTime.toString("HH:mm");
    };

    QList<ScheduleRule>* editRules = new QList<ScheduleRule>(initRules);
    auto refreshList = [&]() {
        ruleList->clear();
        for (const ScheduleRule& r : *editRules)
            ruleList->addItem(formatRule(r));
    };
    refreshList();

    QObject::connect(ruleList, &QListWidget::customContextMenuRequested, [&](const QPoint& pos) {
        QMenu ctxMenu(ruleList);
        int ci = ruleList->currentRow();
        if (ci >= 0 && ci < editRules->size()) {
            ctxMenu.addAction(u"复制规则"_s, [&, ci]() {
                copiedScheduleRules.clear();
                copiedScheduleRules.append((*editRules)[ci]);
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
        addDlg.setFixedWidth(360);
        addDlg.setMinimumHeight(280);
        QVBoxLayout* al = new QVBoxLayout(&addDlg);
        al->addWidget(new QLabel(u"规则类型："_s));
        QComboBox* typeCombo = new QComboBox();
        typeCombo->addItem(u"周期重复"_s);
        typeCombo->addItem(u"固定时间"_s);
        al->addWidget(typeCombo);

        QWidget* periodicWidget = new QWidget();
        QHBoxLayout* pLay = new QHBoxLayout(periodicWidget);
        pLay->setContentsMargins(0, 4, 0, 0);
        QSpinBox* daySpin = new QSpinBox(); daySpin->setRange(0, 365); daySpin->setSuffix(u" 天"_s);
        QSpinBox* hourSpin = new QSpinBox(); hourSpin->setRange(0, 23); hourSpin->setValue(0); hourSpin->setSuffix(u" 小时"_s);
        QSpinBox* minSpin = new QSpinBox(); minSpin->setRange(0, 59); minSpin->setSuffix(u" 分钟"_s);
        pLay->addWidget(daySpin); pLay->addWidget(hourSpin); pLay->addWidget(minSpin);
        al->addWidget(periodicWidget);

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

        // 编辑模式：预填已有规则的值
        if (existing) {
            if (existing->type == ScheduleRule::Periodic) {
                typeCombo->setCurrentIndex(0);
                int total = existing->intervalSecs;
                daySpin->setValue(total / 86400);
                hourSpin->setValue((total % 86400) / 3600);
                minSpin->setValue((total % 3600) / 60);
            } else {
                typeCombo->setCurrentIndex(1);
                periodicWidget->setVisible(false);
                fixedWidget->setVisible(true);
                timeEdit->setTime(existing->fixedTime);
                for (int d = 0; d < 7; d++) {
                    if (existing->daysOfWeek.contains(d + 1))
                        dowChecks[d]->setChecked(true);
                }
            }
        }

        QObject::connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [periodicWidget, fixedWidget](int ci) {
            periodicWidget->setVisible(ci == 0);
            fixedWidget->setVisible(ci == 1);
        });

        al->addStretch();
        QHBoxLayout* abLay = new QHBoxLayout();
        abLay->addStretch();
        QPushButton* okB = new QPushButton(u"确定"_s);
        QPushButton* cancelB = new QPushButton(u"取消"_s);
        QObject::connect(okB, &QPushButton::clicked, &addDlg, &QDialog::accept);
        QObject::connect(cancelB, &QPushButton::clicked, &addDlg, &QDialog::reject);
        abLay->addWidget(okB); abLay->addWidget(cancelB); abLay->addStretch();
        al->addLayout(abLay);

        if (addDlg.exec() != QDialog::Accepted) return;

        ScheduleRule rule;
        if (typeCombo->currentIndex() == 0) {
            rule.type = ScheduleRule::Periodic;
            rule.intervalSecs = daySpin->value() * 86400 + hourSpin->value() * 3600 + minSpin->value() * 60;
            if (rule.intervalSecs <= 0) {
                showMessageDialog(&dlg, u"提示"_s, u"周期不能为 0。"_s);
                return;
            }
        } else {
            rule.type = ScheduleRule::FixedTime;
            rule.fixedTime = timeEdit->time();
            for (int d = 0; d < 7; d++) {
                if (dowChecks[d]->isChecked()) rule.daysOfWeek.insert(d + 1);
            }
        }
        rule.nextTrigger = calculateNextTrigger(rule);
        if (editIndex >= 0 && editIndex < editRules->size()) {
            (*editRules)[editIndex] = rule;
        } else {
            editRules->append(rule);
        }
        refreshList();
    };

    QObject::connect(addBtn, &QPushButton::clicked, [&]() { openRuleDialog(-1); });

    QObject::connect(ruleList, &QListWidget::itemDoubleClicked, [&](QListWidgetItem*) {
        int ci = ruleList->currentRow();
        if (ci >= 0 && ci < editRules->size())
            openRuleDialog(ci);
    });

    QObject::connect(removeBtn, &QPushButton::clicked, [&]() {
        int ci = ruleList->currentRow();
        if (ci >= 0 && ci < editRules->size()) {
            editRules->removeAt(ci);
            refreshList();
        }
    });

    // 定时运行模式下添加"持续运行时长"选项
    QCheckBox* trackDurationCheck = nullptr;
    if (forRun) {
        trackDurationCheck = new QCheckBox(u"监控持续运行时长"_s);
        trackDurationCheck->setToolTip(u"选中时在程序列表中显示“持续运行时长”，未选中则始终显示“-”"_s);
        if (rows.size() == 1) {
            int itemIdx = findItemIndexByPath(rowPath(rows[0]));
            if (itemIdx >= 0)
                trackDurationCheck->setChecked(items[itemIdx].trackRunDuration);
        }
        lay->addWidget(trackDurationCheck);
    }

    lay->addStretch();
    QHBoxLayout* dlgBtnLay = new QHBoxLayout();
    dlgBtnLay->addStretch();
    QPushButton* clearBtn = new QPushButton(u"清空"_s);
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(clearBtn, &QPushButton::clicked, [&]() { editRules->clear(); refreshList(); });
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlgBtnLay->addWidget(clearBtn);
    dlgBtnLay->addWidget(okBtn);
    dlgBtnLay->addWidget(cancelBtn);
    dlgBtnLay->addStretch();
    lay->addLayout(dlgBtnLay);

    if (dlg.exec() != QDialog::Accepted) { delete editRules; return; }

    QList<ScheduleRule> finalRules = *editRules;
    delete editRules;

    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        if (forRun) {
            item.runRules = finalRules;
            item.scheduledRunEnabled = !finalRules.isEmpty();
            if (trackDurationCheck)
                item.trackRunDuration = trackDurationCheck->isChecked();
            if (item.scheduledRunEnabled) {
                item.guarding = false;
                item.restartRulesActive = false;
            }
        } else {
            item.restartRules = finalRules;
            item.restartRulesActive = !finalRules.isEmpty();
            if (item.restartRulesActive)
                item.scheduledRunEnabled = false;
        }
        auto setCell = [&](int col, const QString& text) {
            if (tableWidget->item(row, col)) {
                tableWidget->item(row, col)->setText(text);
                tableWidget->item(row, col)->setToolTip(text);
            }
        };
        if (item.scheduledRunEnabled) {
            setCell(6, formatScheduleRules(item.runRules));
            QDateTime nt = nextTriggerTime(item.runRules);
            setCell(7, nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
        } else {
            setCell(6, item.restartRulesActive ? formatScheduleRules(item.restartRules) : u"-"_s);
            QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
            setCell(7, nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
        }
        setCell(8, item.scheduledRunEnabled ? "-" : formatStartDelay(item.startDelaySecs));

        QWidget* opw = tableWidget->cellWidget(row, 9);
        if (opw) {
            QPushButton* gBtn = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(item.path));
            QPushButton* sBtn = opw->findChild<QPushButton*>(QString("srBtn_%1").arg(item.path));
            QPushButton* rBtn = opw->findChild<QPushButton*>(QString("runBtn_%1").arg(item.path));
            if (gBtn) gBtn->setText(item.guarding ? u"关闭守护"_s : u"开始守护"_s);
            if (sBtn) sBtn->setText(item.restartRulesActive ? u"关闭定时重启"_s : u"开启定时重启"_s);
            if (rBtn) rBtn->setText(item.scheduledRunEnabled ? u"关闭定时运行"_s : u"开启定时运行"_s);
        }
        if (tableWidget->item(row, 1)) {
            if (item.scheduledRunEnabled) tableWidget->item(row, 1)->setText(u"定时运行"_s);
            else if (!item.guarding && !item.restartRulesActive) tableWidget->item(row, 1)->setText(u"未守护"_s);
        }
        updateButtonStates(row);
        logOperation(forRun ? u"设置定时运行规则"_s : u"设置定时重启规则"_s, programId(item.processName, item.launchArgs));
    }
    saveSettings();
}
