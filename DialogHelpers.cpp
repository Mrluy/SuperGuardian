#include "DialogHelpers.h"

const Qt::WindowFlags kDialogFlags = Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint;

bool showMessageDialog(QWidget* parent, const QString& title, const QString& text, bool isQuestion) {
    QDialog dlg(parent, kDialogFlags);
    dlg.setWindowTitle(title);
    dlg.setFixedSize(300, 200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QLabel* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl, 1);
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("\u786e\u5b9a"));
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnLay->addWidget(okBtn);
    if (isQuestion) {
        QPushButton* cancelBtn = new QPushButton(QString::fromUtf8("\u53d6\u6d88"));
        QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLay->addWidget(cancelBtn);
    }
    btnLay->addStretch();
    lay->addLayout(btnLay);
    return dlg.exec() == QDialog::Accepted;
}

QString showItemDialog(QWidget* parent, const QString& title, const QString& label,
                              const QStringList& items, bool* ok) {
    QDialog dlg(parent, kDialogFlags);
    dlg.setWindowTitle(title);
    dlg.setFixedSize(300, 200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(label));
    QComboBox* combo = new QComboBox();
    combo->addItems(items);
    lay->addWidget(combo);
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
    int result = dlg.exec();
    if (ok) *ok = (result == QDialog::Accepted);
    return combo->currentText();
}

int showIntDialog(QWidget* parent, const QString& title, const QString& label,
                         int value, int minVal, int maxVal, int step, bool* ok) {
    QDialog dlg(parent, kDialogFlags);
    dlg.setWindowTitle(title);
    dlg.setFixedSize(300, 200);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(label));
    QSpinBox* spin = new QSpinBox();
    spin->setRange(minVal, maxVal);
    spin->setValue(value);
    spin->setSingleStep(step);
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
    int result = dlg.exec();
    if (ok) *ok = (result == QDialog::Accepted);
    return spin->value();
}

QString formatRestartInterval(int secs) {
    if (secs <= 0) return QStringLiteral("-");
    int totalMins = secs / 60;
    int days = totalMins / 1440;
    int hours = (totalMins % 1440) / 60;
    int mins = totalMins % 60;
    QString detail;
    if (days > 0) detail += QString::number(days) + QString::fromUtf8("\u5929");
    if (hours > 0) detail += QString::number(hours) + QString::fromUtf8("\u5c0f\u65f6");
    if (mins > 0) detail += QString::number(mins) + QString::fromUtf8("\u5206\u949f");
    if (detail.isEmpty()) detail = QString::fromUtf8("1\u5206\u949f");
    return QString::fromUtf8("\u5468\u671f ") + detail;
}

QString formatDaysShort(const QSet<int>& days) {
    if (days.isEmpty()) return QString::fromUtf8("\u6bcf\u5929");
    QSet<int> weekdays = {1,2,3,4,5};
    QSet<int> weekend = {6,7};
    if (days == weekdays) return QString::fromUtf8("\u5de5\u4f5c\u65e5");
    if (days == weekend) return QString::fromUtf8("\u5468\u672b");
    static const char* names[] = { nullptr, "\u5468\u4e00", "\u5468\u4e8c", "\u5468\u4e09", "\u5468\u56db", "\u5468\u4e94", "\u5468\u516d", "\u5468\u65e5" };
    QList<int> sorted(days.begin(), days.end());
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    for (int d : sorted) {
        if (d >= 1 && d <= 7) parts << QString::fromUtf8(names[d]);
    }
    return parts.join(",");
}

QDateTime calculateNextTrigger(const ScheduleRule& rule, const QDateTime& from) {
    if (rule.type == ScheduleRule::Periodic) {
        return from.addSecs(rule.intervalSecs);
    }
    // FixedTime: find next matching day+time
    QDateTime candidate(from.date(), rule.fixedTime);
    if (candidate <= from) candidate = candidate.addDays(1);
    if (rule.daysOfWeek.isEmpty()) return candidate;
    for (int attempt = 0; attempt < 8; ++attempt) {
        int dow = candidate.date().dayOfWeek();
        if (rule.daysOfWeek.contains(dow)) return candidate;
        candidate = candidate.addDays(1);
    }
    return candidate;
}

QDateTime nextTriggerTime(const QList<ScheduleRule>& rules) {
    QDateTime earliest;
    for (const ScheduleRule& r : rules) {
        if (r.nextTrigger.isValid()) {
            if (!earliest.isValid() || r.nextTrigger < earliest)
                earliest = r.nextTrigger;
        }
    }
    return earliest;
}

QString formatScheduleRules(const QList<ScheduleRule>& rules) {
    if (rules.isEmpty()) return QStringLiteral("-");
    if (rules.size() == 1) {
        const ScheduleRule& r = rules[0];
        if (r.type == ScheduleRule::Periodic) return formatRestartInterval(r.intervalSecs);
        return formatDaysShort(r.daysOfWeek) + " " + r.fixedTime.toString("HH:mm");
    }
    return QString::fromUtf8("%1\u4e2a\u89c4\u5219").arg(rules.size());
}
