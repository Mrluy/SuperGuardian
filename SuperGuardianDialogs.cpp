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

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton(QString::fromUtf8("\u6dfb\u52a0"));
    QPushButton* removeBtn = new QPushButton(QString::fromUtf8("\u5220\u9664"));
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    QObject::connect(addBtn, &QPushButton::clicked, [&]() {
        QDialog addDlg(&dlg, kDialogFlags);
        addDlg.setWindowTitle(QString::fromUtf8("\u6dfb\u52a0\u89c4\u5219"));
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
        QSpinBox* hourSpin = new QSpinBox(); hourSpin->setRange(0, 23); hourSpin->setValue(1); hourSpin->setSuffix(QString::fromUtf8(" \u5c0f\u65f6"));
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
        editRules->append(rule);
        refreshList();
    });

    QObject::connect(removeBtn, &QPushButton::clicked, [&]() {
        int ci = ruleList->currentRow();
        if (ci >= 0 && ci < editRules->size()) {
            editRules->removeAt(ci);
            refreshList();
        }
    });

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
            setCell(5, formatScheduleRules(item.runRules));
            QDateTime nt = nextTriggerTime(item.runRules);
            setCell(6, nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
        } else {
            setCell(5, item.restartRulesActive ? formatScheduleRules(item.restartRules) : QStringLiteral("-"));
            QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
            setCell(6, nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
        }
        setCell(7, item.scheduledRunEnabled ? "-" : formatStartDelay(item.startDelaySecs));

        QWidget* opw = tableWidget->cellWidget(row, 8);
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

// ---- 启动延时设置对话框 ----

void SuperGuardian::contextSetStartDelay(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u8bbe\u7f6e\u542f\u52a8\u5ef6\u65f6"));
    dlg.setFixedWidth(320);
    dlg.setMinimumHeight(150);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u8bf7\u8bbe\u7f6e\u542f\u52a8\u5ef6\u65f6\uff08\u79d2\uff09\uff1a")));
    lay->addWidget(new QLabel(QString::fromUtf8("\u7a0b\u5e8f\u91cd\u542f\u65f6\u7684\u5ef6\u65f6\uff0c\u8bbe\u7f6e\u4e3a 0 \u5173\u95ed\u5ef6\u65f6\u3002\n\u5b88\u62a4\u91cd\u542f\u3001\u5b9a\u65f6\u91cd\u542f\u5747\u4f7f\u7528\u6b64\u5ef6\u65f6\u3002")));
    QSpinBox* spin = new QSpinBox();
    spin->setRange(0, 86400);
    spin->setValue(1);
    spin->setSuffix(QString::fromUtf8(" \u79d2"));
    spin->setSpecialValueText(QString::fromUtf8("\u5173\u95ed"));
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) spin->setValue(items[itemIdx].startDelaySecs);
    }
    lay->addWidget(spin);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    int delaySecs = spin->value();
    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        item.startDelaySecs = delaySecs;
        item.startDelayExitTime = QDateTime();
        if (tableWidget->item(row, 7)) {
            if (item.scheduledRunEnabled)
                tableWidget->item(row, 7)->setText("-");
            else
                tableWidget->item(row, 7)->setText(formatStartDelay(delaySecs));
        }
    }
    saveSettings();
}

// ---- 启动参数设置对话框 ----

void SuperGuardian::contextSetLaunchArgs(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u8bbe\u7f6e\u542f\u52a8\u53c2\u6570"));
    dlg.setFixedWidth(450);
    dlg.setMinimumHeight(160);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u8bf7\u8f93\u5165\u542f\u52a8\u53c2\u6570\uff08\u7559\u7a7a\u8868\u793a\u65e0\u53c2\u6570\uff09\uff1a")));
    QLineEdit* argsEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) argsEdit->setText(items[itemIdx].launchArgs);
    }
    lay->addWidget(argsEdit);
    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn); btnLay->addWidget(cancelBtn); btnLay->addStretch();
    lay->addLayout(btnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    QString args = argsEdit->text().trimmed();
    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        items[itemIdx].launchArgs = args;
        QString displayName = args.isEmpty() ? items[itemIdx].processName : (items[itemIdx].processName + " " + args);
        if (tableWidget->item(row, 0)) {
            tableWidget->item(row, 0)->setText(displayName);
            tableWidget->item(row, 0)->setToolTip(displayName);
        }
    }
    saveSettings();
}
