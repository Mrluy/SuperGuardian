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
    QString result;
    if (days > 0) result += QString::number(days) + QString::fromUtf8("\u5929");
    if (hours > 0) result += QString::number(hours) + QString::fromUtf8("\u5c0f\u65f6");
    if (mins > 0) result += QString::number(mins) + QString::fromUtf8("\u5206\u949f");
    if (result.isEmpty()) result = QString::fromUtf8("1\u5206\u949f");
    return result;
}
