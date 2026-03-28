// simplified main: instantiate SuperGuardian window

#include <QtWidgets/QApplication>
#include <QtCore/QCoreApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include "SuperGuardian.h"
#include "AppStorage.h"
#include "ProcessUtils.h"

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName("SuperGuardian");
    QCoreApplication::setOrganizationName("SuperGuardian");

    if (argc > 1 && QString::fromUtf8(argv[1]) == "--watchdog") {
        QCoreApplication app(argc, argv);
        initializeAppStorage();
        return runWatchdogMode(argc, argv);
    }

    QApplication a(argc, argv);
    initializeAppStorage();

    QSharedMemory shared("SuperGuardianSingleton");
    if (!shared.create(1)) {
        {
            QDialog dlg(nullptr, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
            dlg.setWindowTitle(QString::fromUtf8("提示"));
            dlg.setFixedSize(300, 200);
            QVBoxLayout* lay = new QVBoxLayout(&dlg);
            QLabel* lbl = new QLabel(QString::fromUtf8("程序已运行"));
            lbl->setWordWrap(true);
            lbl->setAlignment(Qt::AlignCenter);
            lay->addWidget(lbl, 1);
            QHBoxLayout* btnLay = new QHBoxLayout();
            btnLay->addStretch();
            QPushButton* okBtn = new QPushButton(QString::fromUtf8("确定"));
            QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
            btnLay->addWidget(okBtn);
            btnLay->addStretch();
            lay->addLayout(btnLay);
            dlg.exec();
        }
        return 0;
    }

    SuperGuardian w;
    w.show();
    return a.exec();
}
