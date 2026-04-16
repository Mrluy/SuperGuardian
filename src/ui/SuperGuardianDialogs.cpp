#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ScheduleRuleEditor.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include "ThemeManager.h"
#include <QtWidgets>
#include <QTimeZone>

// 计算规则列表中所有规则在指定月份的合并触发时间
static QList<QDateTime> computeAllRulesInMonth(const QList<ScheduleRule>& rules, int year, int month, int maxCount = 500) {
    QSet<qint64> seen;
    QList<QDateTime> allTimes;
    for (const ScheduleRule& rule : rules) {
        QList<QDateTime> t = computeTriggersInMonth(rule, year, month, maxCount);
        for (const QDateTime& dt : t) {
            qint64 key = dt.toSecsSinceEpoch();
            if (!seen.contains(key)) {
                seen.insert(key);
                allTimes.append(dt);
            }
        }
    }
    std::sort(allTimes.begin(), allTimes.end());
    if (allTimes.size() > maxCount)
        allTimes = allTimes.mid(0, maxCount);
    return allTimes;
}

// 刷新规则列表的月度计划预览面板
static void refreshRulesMonthPreview(QListWidget* previewList, SimpleCalendarGrid* calendar,
                                     const QList<ScheduleRule>& rules) {
    previewList->clear();

    int year = calendar->yearShown();
    int month = calendar->monthShown();

    // 计算触发日期集合用于日历高亮
    QSet<QDate> highlightDates;
    for (const ScheduleRule& rule : rules)
        highlightDates.unite(triggerDatesInMonth(rule, year, month));

    // 计算用于列表显示的触发时间（最多25条）
    QList<QDateTime> displayTimes = computeAllRulesInMonth(rules, year, month, 25);

    bool isDark = currentThemeName() == u"dark"_s;
    QTextCharFormat normalFmt;
    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(isDark ? QColor(33, 70, 111) : QColor(219, 234, 254));
    highlightFmt.setForeground(isDark ? QColor(96, 205, 255) : QColor(0, 95, 183));
    calendar->setDateTextFormat(QDate(), normalFmt);
    for (const QDate& d : highlightDates)
        calendar->setDateTextFormat(d, highlightFmt);

    // 填充触发时间列表（最多25条，按日期分组）
    static constexpr QStringView dowNames[] = { u"", u"周一", u"周二", u"周三", u"周四", u"周五", u"周六", u"周日" };
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
    if (shown > 0) {
        QListWidgetItem* hintItem = new QListWidgetItem(u"仅显示前 25 个示例"_s);
        hintItem->setFlags(hintItem->flags() & ~Qt::ItemIsSelectable);
        hintItem->setForeground(isDark ? QColor(150, 150, 150) : QColor(120, 120, 120));
        hintItem->setTextAlignment(Qt::AlignCenter);
        previewList->addItem(hintItem);
    }
}

void SuperGuardian::contextSetScheduleRules(const QList<int>& rows, bool forRun, bool activateOnConfirm) {
    QList<ScheduleRule> initRules;
    if (rows.size() == 1) {
        int itemIdx = findItemIndexById(rowId(rows[0]));
        if (itemIdx >= 0)
            initRules = forRun ? items[itemIdx].runRules : items[itemIdx].restartRules;
    }

    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(forRun ? u"定时运行规则"_s : u"定时重启规则"_s);
    dlg.setMinimumSize(600, 460);

    QHBoxLayout* mainLay = new QHBoxLayout(&dlg);

    // ===== 左侧：规则列表 =====
    QWidget* leftWidget = new QWidget();
    QVBoxLayout* lay = new QVBoxLayout(leftWidget);
    lay->setContentsMargins(0, 0, 0, 0);
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
        return formatDaysShort(r.daysOfWeek) + " " + r.fixedTime.toString("HH:mm:ss");
    };

    QList<ScheduleRule>* editRules = new QList<ScheduleRule>(initRules);
    std::function<void()> refreshList = [&]() {
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
        if (!editRules->isEmpty()) {
            ctxMenu.addAction(u"复制全部规则"_s, [&]() {
                syncRulesFromList();
                copiedScheduleRules = *editRules;
                if (!copiedScheduleRules.isEmpty())
                    copiedRulesTime = QDateTime::currentDateTime();
            });
        }
        bool canPaste = !copiedScheduleRules.isEmpty() && copiedRulesTime.isValid()
            && copiedRulesTime.secsTo(QDateTime::currentDateTime()) < 7200;
        if (canPaste) {
            ctxMenu.addAction(u"粘贴规则"_s, [&]() {
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
        }
        ctxMenu.exec(ruleList->viewport()->mapToGlobal(pos));
    });

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton(u"添加"_s);
    QPushButton* removeBtn = new QPushButton(u"删除"_s);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    auto openRuleDialog = [&](int editIndex) {
        syncRulesFromList();
        const ScheduleRule* existing = (editIndex >= 0 && editIndex < editRules->size()) ? &(*editRules)[editIndex] : nullptr;
        ScheduleRule result;
        if (!showScheduleRuleEditDialog(&dlg, existing, result)) return;
        if (editIndex >= 0 && editIndex < editRules->size())
            (*editRules)[editIndex] = result;
        else
            editRules->append(result);
        refreshList();
    };

    QObject::connect(addBtn, &QPushButton::clicked, [&]() { openRuleDialog(-1); });

    QObject::connect(ruleList, &QListWidget::itemDoubleClicked, [&](QListWidgetItem*) {
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

    mainLay->addWidget(leftWidget, 1);

    // ===== 右侧：计划预览 =====
    bool isDark = currentThemeName() == u"dark"_s;
    QWidget* previewWidget = new QWidget();
    QVBoxLayout* prevLay = new QVBoxLayout(previewWidget);
    prevLay->setContentsMargins(8, 0, 0, 0);

    QLabel* prevTitle = new QLabel(u"计划预览"_s);
    QFont titleFont = prevTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    prevTitle->setFont(titleFont);
    prevLay->addWidget(prevTitle);

    CalendarWithNav calNav = createCalendarWithNav(isDark);
    SimpleCalendarGrid* previewCalendar = calNav.calendar;
    prevLay->addWidget(calNav.widget);

    QLabel* tzLabel = new QLabel(u"系统时区：%1"_s.arg(
        QString::fromUtf8(QTimeZone::systemTimeZone().id())));
    tzLabel->setStyleSheet(isDark
        ? u"color: #888888; font-size: 11px;"_s
        : u"color: #999999; font-size: 11px;"_s);
    prevLay->addWidget(tzLabel);

    QListWidget* previewTimeList = new QListWidget();
    previewTimeList->setAlternatingRowColors(true);
    prevLay->addWidget(previewTimeList, 1);

    previewWidget->setFixedWidth(280);
    mainLay->addWidget(previewWidget, 0);

    // 在 refreshList 后刷新预览（用 lambda 包装，每次 refreshList 后调用）
    auto refreshPreviewPanel = [&]() {
        syncRulesFromList();
        refreshRulesMonthPreview(previewTimeList, previewCalendar, *editRules);
    };

    // 日历月份导航切换 → 重新计算该月预览
    previewCalendar->setPageChangedCallback([&](int, int) {
        refreshPreviewPanel();
    });

    // 重新定义 refreshList 以包含预览刷新
    refreshList = [&]() {
        ruleList->clear();
        for (int i = 0; i < editRules->size(); i++) {
            QListWidgetItem* item = new QListWidgetItem(formatRule((*editRules)[i]));
            item->setData(Qt::UserRole, i);
            ruleList->addItem(item);
        }
        refreshPreviewPanel();
    };
    // 初始刷新预览
    refreshPreviewPanel();

    if (dlg.exec() != QDialog::Accepted) { delete editRules; return; }

    syncRulesFromList();
    QList<ScheduleRule> finalRules = *editRules;
    delete editRules;

    for (int row : rows) {
        int itemIdx = findItemIndexById(rowId(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        QWidget* opw = tableWidget->cellWidget(row, 10);
        auto setCell = [&](int col, const QString& text) {
            if (tableWidget->item(row, col)) {
                tableWidget->item(row, col)->setText(text);
                tableWidget->item(row, col)->setToolTip(text);
            }
        };
        auto setButtonText = [&](const QString& objectName, const QString& text) {
            if (!opw) return;
            if (QPushButton* button = opw->findChild<QPushButton*>(objectName))
                button->setText(text);
        };
        if (forRun) {
            item.runRules = finalRules;
            if (trackDurationCheck)
                item.trackRunDuration = trackDurationCheck->isChecked();
            if (hideWindowCheck)
                item.runHideWindow = hideWindowCheck->isChecked();
            if (activateOnConfirm) {
                item.scheduledRunEnabled = !item.runRules.isEmpty();
                setButtonText(QString("runBtn_%1").arg(item.id),
                    item.scheduledRunEnabled ? u"关闭定时运行"_s : u"开启定时运行"_s);
            }
        } else {
            item.restartRules = finalRules;
            if (activateOnConfirm) {
                item.restartRulesActive = !item.restartRules.isEmpty();
                setButtonText(QString("srBtn_%1").arg(item.id),
                    item.restartRulesActive ? u"关闭定时重启"_s : u"开启定时重启"_s);
            }
        }
        if (item.scheduledRunEnabled) {
            setCell(2, u"定时运行"_s);
            setCell(7, formatScheduleRules(item.runRules));
            if (tableWidget->item(row, 7))
                tableWidget->item(row, 7)->setToolTip(formatScheduleRulesDetail(item.runRules));
            QDateTime nt = nextTriggerTime(item.runRules);
            setCell(8, nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
        } else if (item.restartRulesActive) {
            if (!item.guarding)
                setCell(2, u"-"_s);
            setCell(7, formatScheduleRules(item.restartRules));
            if (tableWidget->item(row, 7))
                tableWidget->item(row, 7)->setToolTip(formatScheduleRulesDetail(item.restartRules));
            QDateTime nt = nextTriggerTime(item.restartRules);
            setCell(8, nt.isValid() ? nt.toString(u"yyyy年M月d日 hh:mm:ss"_s) : "-");
        } else if (forRun && !item.runRules.isEmpty()) {
            setCell(7, formatScheduleRules(item.runRules));
            if (tableWidget->item(row, 7))
                tableWidget->item(row, 7)->setToolTip(formatScheduleRulesDetail(item.runRules));
            setCell(8, "-");
        } else if (!forRun && !item.restartRules.isEmpty()) {
            setCell(7, formatScheduleRules(item.restartRules));
            if (tableWidget->item(row, 7))
                tableWidget->item(row, 7)->setToolTip(formatScheduleRulesDetail(item.restartRules));
            setCell(8, "-");
        } else {
            if (!item.guarding)
                setCell(2, u"未守护"_s);
            setCell(7, u"-"_s);
            setCell(8, "-");
        }
        setCell(9, item.scheduledRunEnabled ? "-" : formatStartDelay(item.startDelaySecs));
        updateButtonStates(row);
        logOperation(forRun ? u"设置定时运行规则"_s : u"设置定时重启规则"_s, programId(item.processName, item.launchArgs));
    }
    saveSettings();
}
