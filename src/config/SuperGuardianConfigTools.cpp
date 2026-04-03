#include "SuperGuardian.h"
#include "DialogHelpers.h"
#include "ConfigDatabase.h"
#include <QtWidgets>
#include <algorithm>

using namespace Qt::Literals::StringLiterals;

void SuperGuardian::contextSetNote(const QList<int>& rows) {
    QDialog dlg(this, kDialogFlags);
    dlg.setWindowTitle(u"备注"_s);
    dlg.setFixedWidth(400);
    dlg.setMinimumHeight(140);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(u"请输入备注名称（留空表示清除备注）："_s));

    QLineEdit* noteEdit = new QLineEdit();
    if (rows.size() == 1) {
        int itemIdx = findItemIndexByPath(rowPath(rows[0]));
        if (itemIdx >= 0)
            noteEdit->setText(items[itemIdx].note);
    }
    lay->addWidget(noteEdit);
    lay->addStretch();

    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);

    if (dlg.exec() != QDialog::Accepted)
        return;

    QString note = noteEdit->text().trimmed();
    for (int row : rows) {
        int itemIdx = findItemIndexByPath(rowPath(row));
        if (itemIdx < 0)
            continue;

        items[itemIdx].note = note;
        QString fullName = items[itemIdx].launchArgs.isEmpty()
            ? items[itemIdx].processName
            : (items[itemIdx].processName + u" "_s + items[itemIdx].launchArgs);
        QString displayName = note.isEmpty() ? fullName : note;

        if (QTableWidgetItem* item = tableWidget->item(row, 0)) {
            item->setText(displayName);
            item->setToolTip(fullName);
        }
    }
    saveSettings();
}

void SuperGuardian::createDesktopShortcut() {
    QString exePath = QCoreApplication::applicationFilePath();
    QString exeDir = QCoreApplication::applicationDirPath();
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString lnkPath = desktop + u"/超级守护.lnk"_s;
    QString ps = QString(
        "$ws = New-Object -ComObject WScript.Shell; "
        "$sc = $ws.CreateShortcut('%1'); "
        "$sc.TargetPath = '%2'; "
        "$sc.WorkingDirectory = '%3'; "
        "$sc.IconLocation = '%2,0'; "
        "$sc.Save()"
    ).arg(lnkPath.replace("'", "''"),
          QDir::toNativeSeparators(exePath).replace("'", "''"),
          QDir::toNativeSeparators(exeDir).replace("'", "''"));

    QProcess proc;
    proc.start("powershell", { u"-NoProfile"_s, u"-Command"_s, ps });
    proc.waitForFinished(10000);

    showMessageDialog(this, u"桌面快捷方式"_s,
        proc.exitCode() == 0
            ? u"桌面快捷方式已创建：超级守护"_s
            : u"创建快捷方式失败，请检查权限。"_s);
}

void SuperGuardian::performSort() {
    if (sortState == 0 || activeSortSection < 0 || activeSortSection >= 9) {
        std::sort(items.begin(), items.end(), [](const GuardItem& a, const GuardItem& b) {
            if (a.pinned != b.pinned)
                return a.pinned > b.pinned;
            return a.insertionOrder < b.insertionOrder;
        });
        rebuildTableFromItems();
        return;
    }

    Qt::SortOrder order = (sortState == 1) ? Qt::AscendingOrder : Qt::DescendingOrder;
    if (tableWidget->rowCount() == 0 && !items.isEmpty())
        rebuildTableFromItems();

    auto collectRows = [this](bool pinned) {
        QVector<QPair<QString, int>> rows;
        for (int r = 0; r < tableWidget->rowCount(); ++r) {
            int idx = findItemIndexByPath(rowPath(r));
            if (idx < 0 || items[idx].pinned != pinned)
                continue;
            QTableWidgetItem* it = tableWidget->item(r, activeSortSection);
            rows.append({ it ? it->text() : QString(), idx });
        }
        return rows;
    };
    auto sortRows = [order](QVector<QPair<QString, int>>& rows) {
        std::sort(rows.begin(), rows.end(), [order](const auto& a, const auto& b) {
            return (order == Qt::AscendingOrder)
                ? (a.first.localeAwareCompare(b.first) < 0)
                : (a.first.localeAwareCompare(b.first) > 0);
        });
    };

    auto pinnedRows = collectRows(true);
    auto unpinnedRows = collectRows(false);
    sortRows(pinnedRows);
    sortRows(unpinnedRows);

    QVector<GuardItem> newItems;
    for (const auto& pair : pinnedRows)
        newItems.append(items[pair.second]);
    for (const auto& pair : unpinnedRows)
        newItems.append(items[pair.second]);
    items = newItems;
    rebuildTableFromItems();

    QHeaderView* header = tableWidget->horizontalHeader();
    header->setSortIndicatorShown(true);
    header->setSortIndicator(activeSortSection, order);
}

void SuperGuardian::saveSortState() {
    auto& db = ConfigDatabase::instance();
    db.setValue(u"sortSection"_s, activeSortSection);
    db.setValue(u"sortState"_s, sortState);
}
