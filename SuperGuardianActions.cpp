#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
#include "EmailService.h"
#include <QtWidgets>

// ---- 右键菜单与表格行操作 ----

void SuperGuardian::onTableContextMenuRequested(const QPoint& pos) {
    QModelIndex idx = tableWidget->indexAt(pos);
    if (!idx.isValid()) return;
    int row = idx.row();
    int itemIndex = findItemIndexByPath(rowPath(row));
    if (itemIndex < 0) return;

    QList<int> targetRows;
    const auto selectedRows = tableWidget->selectionModel() ? tableWidget->selectionModel()->selectedRows() : QModelIndexList{};
    bool clickedRowAlreadySelected = false;
    for (const QModelIndex& index : selectedRows) {
        if (index.row() == row) {
            clickedRowAlreadySelected = true;
            break;
        }
    }
    if (clickedRowAlreadySelected && !selectedRows.isEmpty()) {
        for (const QModelIndex& index : selectedRows) {
            int selectedIndex = findItemIndexByPath(rowPath(index.row()));
            if (selectedIndex >= 0) {
                targetRows.append(index.row());
            }
        }
    } else {
        targetRows.append(row);
    }
    std::sort(targetRows.begin(), targetRows.end());
    targetRows.erase(std::unique(targetRows.begin(), targetRows.end()), targetRows.end());

    QMenu menu(this);
    menu.addAction(QString::fromUtf8("\u624b\u52a8\u542f\u52a8"), this, [this, targetRows]() { for (int row : targetRows) contextStartProgram(row); });
    menu.addAction(QString::fromUtf8("\u7ec8\u6b62\u8fdb\u7a0b"), this, [this, targetRows]() {
        QString name = targetRows.size() == 1 && tableWidget->item(targetRows[0], 0)
            ? tableWidget->item(targetRows[0], 0)->text() : QString();
        QString msg = targetRows.size() == 1
            ? QString::fromUtf8("\u786e\u8ba4\u7ec8\u6b62\u3010%1\u3011\u7684\u8fdb\u7a0b\u5417\uff1f").arg(name)
            : QString::fromUtf8("\u786e\u8ba4\u7ec8\u6b62\u9009\u4e2d\u7684 %1 \u4e2a\u7a0b\u5e8f\u7684\u8fdb\u7a0b\u5417\uff1f").arg(targetRows.size());
        if (!showMessageDialog(this, QString::fromUtf8("\u7ec8\u6b62\u8fdb\u7a0b"), msg, true)) return;
        for (int row : targetRows) contextKillProgram(row);
    });
    menu.addAction(QString::fromUtf8("\u5f00\u59cb/\u505c\u6b62\u5b88\u62a4"), this, [this, targetRows]() { for (int row : targetRows) contextToggleGuard(row); });
    menu.addAction(QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u89c4\u5219"), this, [this, targetRows]() { contextSetScheduleRules(targetRows, false); });
    menu.addAction(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c\u89c4\u5219"), this, [this, targetRows]() { contextSetScheduleRules(targetRows, true); });
    menu.addAction(QString::fromUtf8("\u8bbe\u7f6e\u542f\u52a8\u5ef6\u65f6"), this, [this, targetRows]() { contextSetStartDelay(targetRows); });
    menu.addAction(QString::fromUtf8("\u91cd\u8bd5\u8bbe\u7f6e"), this, [this, targetRows]() { contextSetRetryConfig(targetRows); });
    menu.addAction(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192\u8bbe\u7f6e"), this, [this, targetRows]() { contextSetEmailNotify(targetRows); });
    menu.addSeparator();
    menu.addAction(QString::fromUtf8("\u79fb\u9664\u9879"), this, [this, targetRows]() {
        QString name = targetRows.size() == 1 && tableWidget->item(targetRows[0], 0)
            ? tableWidget->item(targetRows[0], 0)->text() : QString();
        QString msg = targetRows.size() == 1
            ? QString::fromUtf8("\u786e\u8ba4\u79fb\u9664\u3010%1\u3011\u5417\uff1f").arg(name)
            : QString::fromUtf8("\u786e\u8ba4\u79fb\u9664\u9009\u4e2d\u7684 %1 \u4e2a\u7a0b\u5e8f\u9879\u5417\uff1f").arg(targetRows.size());
        if (!showMessageDialog(this, QString::fromUtf8("\u79fb\u9664\u9879"), msg, true)) return;
        QList<int> rows = targetRows;
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows) contextRemoveItem(row);
    });
    menu.exec(tableWidget->viewport()->mapToGlobal(pos));
}

void SuperGuardian::contextStartProgram(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    launchProgram(items[idx].path);
}

void SuperGuardian::contextKillProgram(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    killProcessesByName(items[idx].processName);
}

void SuperGuardian::contextToggleGuard(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    items[idx].guarding = !items[idx].guarding;
    QWidget* opw = tableWidget->cellWidget(row, 8);
    if (opw) {
        QPushButton* b = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(items[idx].path));
        if (b) b->setText(items[idx].guarding ? QString::fromUtf8("\u5173\u95ed\u5b88\u62a4") : QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
    }
    if (items[idx].guarding) {
        items[idx].startTime = QDateTime::currentDateTime();
        int count = 0;
        bool running = isProcessRunning(items[idx].processName, count);
        if (!running && count == 0) {
            launchProgram(items[idx].path);
            items[idx].lastLaunchTime = QDateTime::currentDateTime();
        }
    } else {
        int displayRow = findRowByPath(items[idx].path);
        if (displayRow >= 0) {
            if (!items[idx].restartRulesActive) {
                if (tableWidget->item(displayRow, 1)) tableWidget->item(displayRow, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
            }
            if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText("-");
        }
    }
    updateButtonStates(row);
    saveSettings();
}

void SuperGuardian::contextRemoveItem(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    items.removeAt(idx);
    tableWidget->removeRow(row);
    saveSettings();
}

// ---- 定时规则设置对话框（多规则，支持周期和固定时间） ----

void SuperGuardian::contextSetScheduleRules(const QList<int>& rows, bool forRun) {
    // Build initial rules from first selected item
    QList<ScheduleRule> initRules;
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) {
            initRules = forRun ? items[itemIdx].runRules : items[itemIdx].restartRules;
        }
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

        // Periodic widgets
        QWidget* periodicWidget = new QWidget();
        QHBoxLayout* pLay = new QHBoxLayout(periodicWidget);
        pLay->setContentsMargins(0, 4, 0, 0);
        QSpinBox* daySpin = new QSpinBox(); daySpin->setRange(0, 365); daySpin->setSuffix(QString::fromUtf8(" \u5929"));
        QSpinBox* hourSpin = new QSpinBox(); hourSpin->setRange(0, 23); hourSpin->setValue(1); hourSpin->setSuffix(QString::fromUtf8(" \u5c0f\u65f6"));
        QSpinBox* minSpin = new QSpinBox(); minSpin->setRange(0, 59); minSpin->setSuffix(QString::fromUtf8(" \u5206\u949f"));
        pLay->addWidget(daySpin); pLay->addWidget(hourSpin); pLay->addWidget(minSpin);
        al->addWidget(periodicWidget);

        // Fixed time widgets
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
                // Disable guard/restart when run is enabled
                item.guarding = false;
                item.restartRulesActive = false;
            }
        } else {
            item.restartRules = finalRules;
            item.restartRulesActive = !finalRules.isEmpty();
            if (item.restartRulesActive) {
                item.scheduledRunEnabled = false;
            }
        }
        // Update UI
        auto setCell = [&](int col, const QString& text) {
            if (tableWidget->item(row, col)) {
                tableWidget->item(row, col)->setText(text);
                tableWidget->item(row, col)->setToolTip(text);
            }
        };
        if (item.scheduledRunEnabled) {
            QString rulesText = formatScheduleRules(item.runRules);
            setCell(5, rulesText);
            QDateTime nt = nextTriggerTime(item.runRules);
            setCell(6, nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
        } else {
            setCell(5, item.restartRulesActive ? formatScheduleRules(item.restartRules) : QStringLiteral("-"));
            QDateTime nt = item.restartRulesActive ? nextTriggerTime(item.restartRules) : QDateTime();
            setCell(6, nt.isValid() ? nt.toString(QString::fromUtf8("yyyy\u5e74M\u6708d\u65e5 hh:mm:ss")) : "-");
        }
        setCell(7, item.scheduledRunEnabled ? "-" : (QString::number(item.startDelaySecs) + QString::fromUtf8(" \u79d2")));

        // Update buttons
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

void SuperGuardian::onTableDoubleClicked(int row, int col) {
    Q_UNUSED(col);
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    GuardItem& item = items[idx];
    if (item.scheduledRunEnabled) return; // run mode, don't toggle guard
    item.guarding = !item.guarding;
    QWidget* opw = tableWidget->cellWidget(row, 8);
    if (opw) {
        QPushButton* b = opw->findChild<QPushButton*>(QString("guardBtn_%1").arg(item.path));
        if (b) b->setText(item.guarding ? QString::fromUtf8("\u5173\u95ed\u5b88\u62a4") : QString::fromUtf8("\u5f00\u59cb\u5b88\u62a4"));
    }
    if (item.guarding) {
        item.startTime = QDateTime::currentDateTime();
        int count = 0;
        bool running = isProcessRunning(item.processName, count);
        if (!running && count == 0) {
            launchProgram(item.path);
            item.lastLaunchTime = QDateTime::currentDateTime();
        }
    } else {
        if (!item.restartRulesActive) {
            if (tableWidget->item(row, 1)) tableWidget->item(row, 1)->setText(QString::fromUtf8("\u672a\u5b88\u62a4"));
        }
        if (tableWidget->item(row, 2)) tableWidget->item(row, 2)->setText("-");
    }
    updateButtonStates(row);
    saveSettings();
}

void SuperGuardian::contextSetStartDelay(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u8bbe\u7f6e\u542f\u52a8\u5ef6\u65f6"));
    dlg.setFixedWidth(320);
    dlg.setMinimumHeight(150);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u8bf7\u8bbe\u7f6e\u542f\u52a8\u5ef6\u65f6\uff08\u79d2\uff09\uff1a")));
    lay->addWidget(new QLabel(QString::fromUtf8("\u7a0b\u5e8f\u91cd\u542f\u65f6\u7684\u5ef6\u65f6\uff0c\u6700\u5c0f 1 \u79d2\u3002\n\u5b88\u62a4\u91cd\u542f\u3001\u5b9a\u65f6\u91cd\u542f\u5747\u4f7f\u7528\u6b64\u5ef6\u65f6\u3002")));
    QSpinBox* spin = new QSpinBox();
    spin->setRange(1, 86400);
    spin->setValue(1);
    spin->setSuffix(QString::fromUtf8(" \u79d2"));
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
                tableWidget->item(row, 7)->setText(QString::number(delaySecs) + QString::fromUtf8(" \u79d2"));
        }
    }
    saveSettings();
}

void SuperGuardian::contextSetRetryConfig(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u91cd\u8bd5\u8bbe\u7f6e"));
    dlg.setFixedWidth(360);
    dlg.setMinimumHeight(250);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u542f\u52a8\u5931\u8d25\u540e\u7684\u91cd\u8bd5\u914d\u7f6e\uff1a")));

    QFormLayout* form = new QFormLayout();
    QSpinBox* intervalSpin = new QSpinBox(); intervalSpin->setRange(5, 3600); intervalSpin->setValue(30); intervalSpin->setSuffix(QString::fromUtf8(" \u79d2"));
    QSpinBox* maxRetriesSpin = new QSpinBox(); maxRetriesSpin->setRange(0, 9999); maxRetriesSpin->setValue(10);
    maxRetriesSpin->setSpecialValueText(QString::fromUtf8("\u65e0\u9650\u5236"));
    QSpinBox* maxDurSpin = new QSpinBox(); maxDurSpin->setRange(0, 86400); maxDurSpin->setValue(300); maxDurSpin->setSuffix(QString::fromUtf8(" \u79d2"));
    maxDurSpin->setSpecialValueText(QString::fromUtf8("\u65e0\u9650\u5236"));
    form->addRow(QString::fromUtf8("\u91cd\u8bd5\u95f4\u9694\uff1a"), intervalSpin);
    form->addRow(QString::fromUtf8("\u6700\u5927\u91cd\u8bd5\u6b21\u6570\uff1a"), maxRetriesSpin);
    form->addRow(QString::fromUtf8("\u6700\u5927\u91cd\u8bd5\u65f6\u957f\uff1a"), maxDurSpin);
    lay->addLayout(form);

    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) {
            intervalSpin->setValue(items[itemIdx].retryConfig.retryIntervalSecs);
            maxRetriesSpin->setValue(items[itemIdx].retryConfig.maxRetries);
            maxDurSpin->setValue(items[itemIdx].retryConfig.maxDurationSecs);
        }
    }

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

    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        items[itemIdx].retryConfig.retryIntervalSecs = intervalSpin->value();
        items[itemIdx].retryConfig.maxRetries = maxRetriesSpin->value();
        items[itemIdx].retryConfig.maxDurationSecs = maxDurSpin->value();
    }
    saveSettings();
}

void SuperGuardian::contextSetEmailNotify(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192\u8bbe\u7f6e"));
    dlg.setFixedWidth(360);
    dlg.setMinimumHeight(300);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QCheckBox* enabledCb = new QCheckBox(QString::fromUtf8("\u542f\u7528\u90ae\u4ef6\u63d0\u9192"));
    enabledCb->setChecked(true);
    lay->addWidget(enabledCb);
    lay->addWidget(new QLabel(QString::fromUtf8("\u63d0\u9192\u4e8b\u4ef6\uff1a")));

    QCheckBox* cbGuardTriggered = new QCheckBox(QString::fromUtf8("\u5b88\u62a4\u89e6\u53d1\u91cd\u542f"));
    QCheckBox* cbStartFailed = new QCheckBox(QString::fromUtf8("\u542f\u52a8\u5931\u8d25"));
    QCheckBox* cbRestartFailed = new QCheckBox(QString::fromUtf8("\u5b9a\u65f6\u91cd\u542f\u5931\u8d25"));
    QCheckBox* cbRunFailed = new QCheckBox(QString::fromUtf8("\u5b9a\u65f6\u8fd0\u884c\u5931\u8d25"));
    QCheckBox* cbExited = new QCheckBox(QString::fromUtf8("\u8fdb\u7a0b\u9000\u51fa"));
    QCheckBox* cbRetryExhausted = new QCheckBox(QString::fromUtf8("\u91cd\u8bd5\u8017\u5c3d"));
    cbStartFailed->setChecked(true); cbRestartFailed->setChecked(true);
    cbRunFailed->setChecked(true); cbRetryExhausted->setChecked(true);

    lay->addWidget(cbGuardTriggered);
    lay->addWidget(cbStartFailed);
    lay->addWidget(cbRestartFailed);
    lay->addWidget(cbRunFailed);
    lay->addWidget(cbExited);
    lay->addWidget(cbRetryExhausted);

    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) {
            const EmailNotifyConfig& en = items[itemIdx].emailNotify;
            enabledCb->setChecked(en.enabled);
            cbGuardTriggered->setChecked(en.onGuardTriggered);
            cbStartFailed->setChecked(en.onStartFailed);
            cbRestartFailed->setChecked(en.onScheduledRestartFailed);
            cbRunFailed->setChecked(en.onScheduledRunFailed);
            cbExited->setChecked(en.onProcessExited);
            cbRetryExhausted->setChecked(en.onRetryExhausted);
        }
    }

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

    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        EmailNotifyConfig& en = items[itemIdx].emailNotify;
        en.enabled = enabledCb->isChecked();
        en.onGuardTriggered = cbGuardTriggered->isChecked();
        en.onStartFailed = cbStartFailed->isChecked();
        en.onScheduledRestartFailed = cbRestartFailed->isChecked();
        en.onScheduledRunFailed = cbRunFailed->isChecked();
        en.onProcessExited = cbExited->isChecked();
        en.onRetryExhausted = cbRetryExhausted->isChecked();
    }
    saveSettings();
}

// ---- SMTP 邮件配置对话框 ----

void SuperGuardian::showSmtpConfigDialog() {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u90ae\u4ef6\u63d0\u9192\u914d\u7f6e"));
    dlg.setMinimumSize(400, 420);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QFormLayout* form = new QFormLayout();
    QLineEdit* serverEdit = new QLineEdit(smtpConfig.server);
    QSpinBox* portSpin = new QSpinBox(); portSpin->setRange(1, 65535); portSpin->setValue(smtpConfig.port);
    QCheckBox* tlsCb = new QCheckBox(QString::fromUtf8("TLS/SSL")); tlsCb->setChecked(smtpConfig.useTls);
    QLineEdit* userEdit = new QLineEdit(smtpConfig.username);
    QLineEdit* passEdit = new QLineEdit(smtpConfig.password); passEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* fromEdit = new QLineEdit(smtpConfig.fromAddress);
    QLineEdit* fromNameEdit = new QLineEdit(smtpConfig.fromName);
    QLineEdit* toEdit = new QLineEdit(smtpConfig.toAddress);

    form->addRow(QString::fromUtf8("SMTP \u670d\u52a1\u5668\uff1a"), serverEdit);
    form->addRow(QString::fromUtf8("\u7aef\u53e3\uff1a"), portSpin);
    form->addRow(QString::fromUtf8("\u52a0\u5bc6\uff1a"), tlsCb);
    form->addRow(QString::fromUtf8("\u7528\u6237\u540d\uff1a"), userEdit);
    form->addRow(QString::fromUtf8("\u5bc6\u7801\uff1a"), passEdit);
    form->addRow(QString::fromUtf8("\u53d1\u4ef6\u4eba\u5730\u5740\uff1a"), fromEdit);
    form->addRow(QString::fromUtf8("\u53d1\u4ef6\u4eba\u540d\u79f0\uff1a"), fromNameEdit);
    form->addRow(QString::fromUtf8("\u6536\u4ef6\u4eba\u5730\u5740\uff1a"), toEdit);
    lay->addLayout(form);

    QPushButton* testBtn = new QPushButton(QString::fromUtf8("\u53d1\u9001\u6d4b\u8bd5\u90ae\u4ef6"));
    lay->addWidget(testBtn);

    QObject::connect(testBtn, &QPushButton::clicked, [&]() {
        SmtpConfig test;
        test.server = serverEdit->text();
        test.port = portSpin->value();
        test.useTls = tlsCb->isChecked();
        test.username = userEdit->text();
        test.password = passEdit->text();
        test.fromAddress = fromEdit->text();
        test.fromName = fromNameEdit->text();
        test.toAddress = toEdit->text();
        testBtn->setEnabled(false);
        testBtn->setText(QString::fromUtf8("\u53d1\u9001\u4e2d..."));
        QApplication::processEvents();
        bool ok = sendTestEmail(test);
        testBtn->setEnabled(true);
        testBtn->setText(QString::fromUtf8("\u53d1\u9001\u6d4b\u8bd5\u90ae\u4ef6"));
        showMessageDialog(&dlg, QString::fromUtf8("\u6d4b\u8bd5\u90ae\u4ef6"),
            ok ? QString::fromUtf8("\u6d4b\u8bd5\u90ae\u4ef6\u53d1\u9001\u6210\u529f\uff01")
               : QString::fromUtf8("\u6d4b\u8bd5\u90ae\u4ef6\u53d1\u9001\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u914d\u7f6e\u3002"));
    });

    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u4fdd\u5b58"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn); btnLay->addWidget(cancelBtn); btnLay->addStretch();
    lay->addLayout(btnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    smtpConfig.server = serverEdit->text();
    smtpConfig.port = portSpin->value();
    smtpConfig.useTls = tlsCb->isChecked();
    smtpConfig.username = userEdit->text();
    smtpConfig.password = passEdit->text();
    smtpConfig.fromAddress = fromEdit->text();
    smtpConfig.fromName = fromNameEdit->text();
    smtpConfig.toAddress = toEdit->text();
    saveSettings();
}
