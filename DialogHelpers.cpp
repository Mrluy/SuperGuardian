#include "DialogHelpers.h"

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
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnLay->addWidget(okBtn);
    if (isQuestion) {
        QPushButton* cancelBtn = new QPushButton(u"取消"_s);
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
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
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
    QPushButton* okBtn = new QPushButton(u"确定"_s);
    QPushButton* cancelBtn = new QPushButton(u"取消"_s);
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
    if (secs <= 0) return u"-"_s;
    int totalMins = secs / 60;
    int days = totalMins / 1440;
    int hours = (totalMins % 1440) / 60;
    int mins = totalMins % 60;
    QString detail;
    if (days > 0) detail += QString::number(days) + u"天"_s;
    if (hours > 0) detail += QString::number(hours) + u"小时"_s;
    if (mins > 0) detail += QString::number(mins) + u"分钟"_s;
    if (detail.isEmpty()) detail = u"1分钟"_s;
    return u"周期 "_s + detail;
}

QString formatDaysShort(const QSet<int>& days) {
    if (days.isEmpty()) return u"每天"_s;
    static const QSet<int> weekdays = {1,2,3,4,5};
    static const QSet<int> weekend = {6,7};
    if (days == weekdays) return u"工作日"_s;
    if (days == weekend) return u"周末"_s;
    static constexpr QStringView names[] = { {}, u"周一", u"周二", u"周三", u"周四", u"周五", u"周六", u"周日" };
    QList<int> sorted(days.cbegin(), days.cend());
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    for (int d : sorted) {
        if (d >= 1 && d <= 7) parts << names[d].toString();
    }
    return parts.join(u","_s);
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
    if (rules.isEmpty()) return u"-"_s;
    if (rules.size() == 1) {
        const ScheduleRule& r = rules[0];
        if (r.type == ScheduleRule::Periodic) return formatRestartInterval(r.intervalSecs);
        return formatDaysShort(r.daysOfWeek) + u" "_s + r.fixedTime.toString(u"HH:mm"_s);
    }
    return u"%1个规则"_s.arg(rules.size());
}
