#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
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
    dlg.setWindowTitle(forRun ? QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c\u89c4\u5219") : QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u89c4\u5219"));
    dlg.setMinimumSize(420, 380);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u89c4\u5219\u5217\u8868\uff08\u53ef\u6dfb\u52a0\u591a\u4e2a\uff0c\u65f6\u95f4\u91cd\u590d\u65f6\u53ea\u6267\u884c\u4e00\u6b21\uff09\uff1a")));

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
            ctxMenu.addAction(QString::fromUtf8("\u590d\u5236\u89c4\u5219"), [&, ci]() {
                copiedScheduleRules.clear();
                copiedScheduleRules.append((*editRules)[ci]);
                copiedRulesTime = QDateTime::currentDateTime();
            });
        }
        bool canPaste = !copiedScheduleRules.isEmpty() && copiedRulesTime.isValid()
            && copiedRulesTime.secsTo(QDateTime::currentDateTime()) < 7200;
        QAction* pasteAct = ctxMenu.addAction(QString::fromUtf8("\u7c98\u8d34\u89c4\u5219"), [&]() {
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
    QPushButton* addBtn = new QPushButton(QString::fromUtf8("\u6dfb\u52a0"));
    QPushButton* removeBtn = new QPushButton(QString::fromUtf8("\u5220\u9664"));
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    // editIndex < 0 表示添加新规则，>= 0 表示编辑已有规则
    auto openRuleDialog = [&](int editIndex) {
        const ScheduleRule* existing = (editIndex >= 0 && editIndex < editRules->size()) ? &(*editRules)[editIndex] : nullptr;
        QDialog addDlg(&dlg, kDialogFlags);
        addDlg.setWindowTitle(existing ? QString::fromUtf8("\u7f16\u8f91\u89c4\u5219") : QString::fromUtf8("\u6dfb\u52a0\u89c4\u5219"));
        addDlg.setFixedWidth(360);
        addDlg.setMinimumHeight(280);
        QVBoxLayout* al = new QVBoxLayout(&addDlg);
        al->addWidget(new QLabel(QString::fromUtf8("\u89c4\u5219\u7c7b\u578b\uff1a")));
        QComboBox* typeCombo = new QComboBox();
        typeCombo->addItem(QString::fromUtf8("\u5468\u671f\u91cd\u590d"));
        typeCombo->addItem(QString::fromUtf8("\u56fa\u5b9a\u65f6\u95f4"));
        al->addWidget(typeCombo);

        QWidget* periodicWidget = new QWidget();
        QHBoxLayout* pLay = new QHBoxLayout(periodicWidget);
        pLay->setContentsMargins(0, 4, 0, 0);
        QSpinBox* daySpin = new QSpinBox(); daySpin->setRange(0, 365); daySpin->setSuffix(QString::fromUtf8(" \u5929"));
        QSpinBox* hourSpin = new QSpinBox(); hourSpin->setRange(0, 23); hourSpin->setValue(0); hourSpin->setSuffix(QString::fromUtf8(" \u5c0f\u65f6"));
        QSpinBox* minSpin = new QSpinBox(); minSpin->setRange(0, 59); minSpin->setSuffix(QString::fromUtf8(" \u5206\u949f"));
        pLay->addWidget(daySpin); pLay->addWidget(hourSpin); pLay->addWidget(minSpin);
        al->addWidget(periodicWidget);

        QWidget* fixedWidget = new QWidget();
        QVBoxLayout* fLay = new QVBoxLayout(fixedWidget);
        fLay->setContentsMargins(0, 4, 0, 0);
        fLay->addWidget(new QLabel(QString::fromUtf8("\u65f6\u95f4\uff1a")));
        QTimeEdit* timeEdit = new QTimeEdit(QTime(0, 0));
        timeEdit->setDisplayFormat("HH:mm");
        fLay->addWidget(timeEdit);
        fLay->addWidget(new QLabel(QString::fromUtf8("\u661f\u671f\uff08\u4e0d\u9009\u5219\u6bcf\u5929\uff09\uff1a")));
        QHBoxLayout* dowLay = new QHBoxLayout();
        static const char* dayNames[] = { "\u4e00", "\u4e8c", "\u4e09", "\u56db", "\u4e94", "\u516d", "\u65e5" };
        QCheckBox* dowChecks[7];
        for (int d = 0; d < 7; d++) {
            dowChecks[d] = new QCheckBox(QString::fromUtf8(dayNames[d]));
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
        QPushButton* okB = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
        QPushButton* cancelB = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
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
                showMessageDialog(&dlg, QString::fromUtf8("\u63d0\u793a"), QString::fromUtf8("\u5468\u671f\u4e0d\u80fd\u4e3a 0\u3002"));
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
        trackDurationCheck = new QCheckBox(QString::fromUtf8("\u76d1\u63a7\u6301\u7eed\u8fd0\u884c\u65f6\u957f"));
        trackDurationCheck->setToolTip(QString::fromUtf8("\u9009\u4e2d\u65f6\u5728\u7a0b\u5e8f\u5217\u8868\u4e2d\u663e\u793a\u201c\u6301\u7eed\u8fd0\u884c\u65f6\u957f\u201d\uff0c\u672a\u9009\u4e2d\u5219\u59cb\u7ec8\u663e\u793a\u201c-\u201d"));
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
    QPushButton* clearBtn = new QPushButton(QString::fromUtf8("\u6e05\u7a7a"));
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
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
            setCell(7, nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
        } else {
            setCell(6, item.restartRulesActive ? formatScheduleRules(item.restartRules) : QStringLiteral("-"));
            QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
            setCell(7, nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
        }
        setCell(8, item.scheduledRunEnabled ? "-" : formatStartDelay(item.startDelaySecs));

        QWidget* opw = tableWidget->cellWidget(row, 9);
        if (opw) {
            QPushButton* gBtn = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(item.path));
            QPushButton* sBtn = opw->findChild<QPushButton*>(QString("srBtn_%1").arg(item.path));
            QPushButton* rBtn = opw->findChild<QPushButton*>(QString("runBtn_%1").arg(item.path));
            if (gBtn) gBtn->setText(item.guarding ? QString::fromUtf8("\u5173\u95ed\u5b88\u62a4") : QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
            if (sBtn) sBtn->setText(item.restartRulesActive ? QString::fromUtf8("\u5173\u95ed\u5b9a\u65f6\u91cd\u542f") : QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u91cd\u542f"));
            if (rBtn) rBtn->setText(item.scheduledRunEnabled ? QString::fromUtf8("\u5173\u95ed\u5b9a\u65f6\u8fd0\u884c") : QString::fromUtf8("\u5f00\u542f\u5b9a\u65f6\u8fd0\u884c"));
        }
        if (tableWidget->item(row, 1)) {
            if (item.scheduledRunEnabled) tableWidget->item(row, 1)->setText(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c"));
            else if (!item.guarding && !item.restartRulesActive) tableWidget->item(row, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
        }
        updateButtonStates(row);
    }
    saveSettings();
}
