/********************************************************************************
** Form generated from reading UI file 'progress_dialog.ui'
**
** Created by: Qt User Interface Compiler version 6.11.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PROGRESS_DIALOG_H
#define UI_PROGRESS_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_ProgressDialog
{
public:
    QVBoxLayout *rootLayout;
    QLabel *filenameLabel;
    QProgressBar *progressBar;
    QHBoxLayout *statsLayout;
    QLabel *speedLabel;
    QSpacerItem *statsSpacer;
    QLabel *timeLabel;
    QSpacerItem *verticalSpacer;
    QHBoxLayout *buttonLayout;
    QSpacerItem *buttonSpacer;
    QPushButton *cancelButton;

    void setupUi(QDialog *ProgressDialog)
    {
        if (ProgressDialog->objectName().isEmpty())
            ProgressDialog->setObjectName("ProgressDialog");
        ProgressDialog->resize(440, 180);
        rootLayout = new QVBoxLayout(ProgressDialog);
        rootLayout->setObjectName("rootLayout");
        filenameLabel = new QLabel(ProgressDialog);
        filenameLabel->setObjectName("filenameLabel");
        filenameLabel->setText(QString::fromUtf8("filename"));
        filenameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        rootLayout->addWidget(filenameLabel);

        progressBar = new QProgressBar(ProgressDialog);
        progressBar->setObjectName("progressBar");
        progressBar->setValue(0);

        rootLayout->addWidget(progressBar);

        statsLayout = new QHBoxLayout();
        statsLayout->setObjectName("statsLayout");
        speedLabel = new QLabel(ProgressDialog);
        speedLabel->setObjectName("speedLabel");
        speedLabel->setText(QString::fromUtf8("0 B/s"));

        statsLayout->addWidget(speedLabel);

        statsSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        statsLayout->addItem(statsSpacer);

        timeLabel = new QLabel(ProgressDialog);
        timeLabel->setObjectName("timeLabel");
        timeLabel->setText(QString::fromUtf8("Remaining: --:--:--"));

        statsLayout->addWidget(timeLabel);


        rootLayout->addLayout(statsLayout);

        verticalSpacer = new QSpacerItem(20, 12, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        rootLayout->addItem(verticalSpacer);

        buttonLayout = new QHBoxLayout();
        buttonLayout->setObjectName("buttonLayout");
        buttonSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        buttonLayout->addItem(buttonSpacer);

        cancelButton = new QPushButton(ProgressDialog);
        cancelButton->setObjectName("cancelButton");

        buttonLayout->addWidget(cancelButton);


        rootLayout->addLayout(buttonLayout);


        retranslateUi(ProgressDialog);

        QMetaObject::connectSlotsByName(ProgressDialog);
    } // setupUi

    void retranslateUi(QDialog *ProgressDialog)
    {
        ProgressDialog->setWindowTitle(QCoreApplication::translate("ProgressDialog", "Transferring file", nullptr));
        cancelButton->setText(QCoreApplication::translate("ProgressDialog", "Cancel", nullptr));
    } // retranslateUi

};

namespace Ui {
    class ProgressDialog: public Ui_ProgressDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PROGRESS_DIALOG_H
