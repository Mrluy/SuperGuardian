#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ProcessUtils.h"
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
    menu.addAction(QString::fromUtf8("手动启动"), this, [this, targetRows]() { for (int row : targetRows) contextStartProgram(row); });
    menu.addAction(QString::fromUtf8("终止进程"), this, [this, targetRows]() {
        QString name = targetRows.size() == 1 && tableWidget->item(targetRows[0], 0)
            ? tableWidget->item(targetRows[0], 0)->text() : QString();
        QString msg = targetRows.size() == 1
            ? QString::fromUtf8("确认终止【%1】的进程吗？").arg(name)
            : QString::fromUtf8("确认终止选中的 %1 个程序的进程吗？").arg(targetRows.size());
        if (!showMessageDialog(this, QString::fromUtf8("终止进程"), msg, true)) return;
        for (int row : targetRows) contextKillProgram(row);
    });
    menu.addAction(QString::fromUtf8("开始/停止守护"), this, [this, targetRows]() { for (int row : targetRows) contextToggleGuard(row); });
    menu.addAction(QString::fromUtf8("定时重启"), this, [this, targetRows]() { contextSetScheduledRestart(targetRows); });
    menu.addAction(QString::fromUtf8("设置守护延时"), this, [this, targetRows]() { contextSetGuardDelay(targetRows); });
    menu.addSeparator();
    menu.addAction(QString::fromUtf8("移除项"), this, [this, targetRows]() {
        QString name = targetRows.size() == 1 && tableWidget->item(targetRows[0], 0)
            ? tableWidget->item(targetRows[0], 0)->text() : QString();
        QString msg = targetRows.size() == 1
            ? QString::fromUtf8("确认移除【%1】吗？").arg(name)
            : QString::fromUtf8("确认移除选中的 %1 个程序项吗？").arg(targetRows.size());
        if (!showMessageDialog(this, QString::fromUtf8("移除项"), msg, true)) return;
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
        QList<QPushButton*> btns = opw->findChildren<QPushButton*>();
        for (QPushButton* b : btns) {
            if (!b->objectName().startsWith("srBtn_")) {
                b->setText(items[idx].guarding ? QString::fromUtf8("关闭守护") : QString::fromUtf8("开始守护"));
                break;
            }
        }
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
            if (items[idx].scheduledRestartIntervalSecs <= 0) {
                if (tableWidget->item(displayRow, 1)) tableWidget->item(displayRow, 1)->setText(QString::fromUtf8("未守护"));
            }
            if (tableWidget->item(displayRow, 2)) tableWidget->item(displayRow, 2)->setText("-");
        }
    }
}

void SuperGuardian::contextRemoveItem(int row) {
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    items.removeAt(idx);
    tableWidget->removeRow(row);
}

void SuperGuardian::contextSetScheduledRestart(const QList<int>& rows) {
    QStringList choices;
    choices << QString::fromUtf8("不定时重启")
            << QString::fromUtf8("每 30 分钟")
            << QString::fromUtf8("每 1 小时")
            << QString::fromUtf8("每 2 小时")
            << QString::fromUtf8("每 6 小时")
            << QString::fromUtf8("每 12 小时")
            << QString::fromUtf8("每 24 小时")
            << QString::fromUtf8("自定义");

    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("定时重启"));
    dlg.setFixedWidth(360);
    dlg.setMinimumHeight(180);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("请选择定时重启间隔：")));
    QComboBox* combo = new QComboBox();
    combo->addItems(choices);
    lay->addWidget(combo);

    QWidget* customWidget = new QWidget();
    QHBoxLayout* customLay = new QHBoxLayout(customWidget);
    customLay->setContentsMargins(0,4,0,0);
    QSpinBox* daySpin = new QSpinBox();
    daySpin->setRange(0, 365);
    daySpin->setValue(0);
    daySpin->setSuffix(QString::fromUtf8(" 天"));
    QSpinBox* hourSpin = new QSpinBox();
    hourSpin->setRange(0, 23);
    hourSpin->setValue(1);
    hourSpin->setSuffix(QString::fromUtf8(" 小时"));
    QSpinBox* minSpin = new QSpinBox();
    minSpin->setRange(0, 59);
    minSpin->setValue(0);
    minSpin->setSuffix(QString::fromUtf8(" 分钟"));
    customLay->addWidget(daySpin);
    customLay->addWidget(hourSpin);
    customLay->addWidget(minSpin);
    customWidget->setVisible(false);
    lay->addWidget(customWidget);

    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [customWidget, &dlg](int ci) {
        customWidget->setVisible(ci == 7);
        dlg.adjustSize();
    });

    lay->addStretch();
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("确定"));
    QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("取消"));
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    if (dlg.exec() != QDialog::Accepted) return;

    int intervalSecs = 0;
    int ci = combo->currentIndex();
    switch (ci) {
    case 0: intervalSecs = 0; break;
    case 1: intervalSecs = 30 * 60; break;
    case 2: intervalSecs = 60 * 60; break;
    case 3: intervalSecs = 2 * 3600; break;
    case 4: intervalSecs = 6 * 3600; break;
    case 5: intervalSecs = 12 * 3600; break;
    case 6: intervalSecs = 24 * 3600; break;
    case 7: intervalSecs = daySpin->value() * 86400 + hourSpin->value() * 3600 + minSpin->value() * 60; break;
    default: return;
    }

    if (ci == 7 && intervalSecs == 0) {
        showMessageDialog(this, QString::fromUtf8("提示"), QString::fromUtf8("自定义间隔不能为0。"));
        return;
    }

    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0) continue;
        GuardItem& item = items[itemIdx];
        item.scheduledRestartIntervalSecs = intervalSecs;
        if (intervalSecs > 0) {
            item.nextScheduledRestart = QDateTime::currentDateTime().addSecs(intervalSecs);
        } else {
            item.nextScheduledRestart = QDateTime();
        }
        if (tableWidget->item(row, 5))
            tableWidget->item(row, 5)->setText(formatRestartInterval(item.scheduledRestartIntervalSecs));
        if (tableWidget->item(row, 6)) {
            tableWidget->item(row, 6)->setText(item.nextScheduledRestart.isValid()
                ? item.nextScheduledRestart.toString(QString::fromUtf8("yyyy年M月d日 hh:mm:ss"))
                : "-");
        }
        // update the scheduled restart button in column 7
        QPushButton* srBtn = tableWidget->findChild<QPushButton*>(QString("srBtn_%1").arg(item.path));
        if (srBtn) srBtn->setText(intervalSecs > 0 ? QString::fromUtf8("停止定时重启") : QString::fromUtf8("开启定时重启"));
    }
    saveSettings();
}

void SuperGuardian::onTableDoubleClicked(int row, int col) {
    Q_UNUSED(col);
    int idx = findItemIndexByPath(rowPath(row));
    if (idx < 0) return;
    GuardItem& item = items[idx];
    item.guarding = !item.guarding;
    QWidget* opw = tableWidget->cellWidget(row,8);
    if (opw) {
        QList<QPushButton*> btns = opw->findChildren<QPushButton*>();
        for (QPushButton* b : btns) {
            if (!b->objectName().startsWith("srBtn_")) {
                b->setText(item.guarding ? QString::fromUtf8("关闭守护") : QString::fromUtf8("开始守护"));
                break;
            }
        }
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
        if (item.scheduledRestartIntervalSecs <= 0) {
            if (tableWidget->item(row,1)) tableWidget->item(row,1)->setText(QString::fromUtf8("未守护"));
        }
        if (tableWidget->item(row,2)) tableWidget->item(row,2)->setText("-");
    }
}

void SuperGuardian::contextSetGuardDelay(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(QString::fromUtf8("\u8bbe\u7f6e\u5b88\u62a4\u5ef6\u65f6"));
    dlg.setFixedWidth(320);
    dlg.setMinimumHeight(150);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QString::fromUtf8("\u8bf7\u8bbe\u7f6e\u5b88\u62a4\u5ef6\u65f6\u542f\u52a8\u65f6\u957f\uff08\u79d2\uff09\uff1a")));
    lay->addWidget(new QLabel(QString::fromUtf8("\u7a0b\u5e8f\u9000\u51fa\u540e\uff0c\u5ef6\u65f6\u6307\u5b9a\u79d2\u6570\u518d\u542f\u52a8\u3002\n\u8bbe\u7f6e\u4e3a 0 \u8868\u793a\u4e0d\u5ef6\u65f6\u3002")));
    QSpinBox* spin = new QSpinBox();
    spin->setRange(0, 86400);
    spin->setValue(0);
    spin->setSuffix(QString::fromUtf8(" \u79d2"));
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0) spin->setValue(items[itemIdx].guardDelaySecs);
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
        item.guardDelaySecs = delaySecs;
        item.guardDelayExitTime = QDateTime();
        if (tableWidget->item(row, 7))
            tableWidget->item(row, 7)->setText(delaySecs > 0 ? QString::number(delaySecs) + QString::fromUtf8(" \u79d2") : "-");
    }
    saveSettings();
}
