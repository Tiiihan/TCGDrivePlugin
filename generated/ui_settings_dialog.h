/********************************************************************************
** Form generated from reading UI file 'settings_dialog.ui'
**
** Created by: Qt User Interface Compiler version 6.11.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SETTINGS_DIALOG_H
#define UI_SETTINGS_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_SettingsDialog
{
public:
    QVBoxLayout *rootLayout;
    QLabel *accountsHeaderLabel;
    QListWidget *accountsList;
    QHBoxLayout *accountButtonsLayout;
    QPushButton *addAccountButton;
    QPushButton *removeAccountButton;
    QPushButton *setDefaultButton;
    QSpacerItem *accountButtonsSpacer;
    QGroupBox *settingsGroupBox;
    QFormLayout *settingsFormLayout;
    QLabel *cacheTtlLabel;
    QSpinBox *cacheTtlSpin;
    QLabel *languageLabel;
    QComboBox *languageCombo;
    QLabel *logLevelLabel;
    QComboBox *logLevelCombo;
    QCheckBox *exportDocsCheck;
    QCheckBox *exportSheetsCheck;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *SettingsDialog)
    {
        if (SettingsDialog->objectName().isEmpty())
            SettingsDialog->setObjectName("SettingsDialog");
        SettingsDialog->resize(480, 450);
        rootLayout = new QVBoxLayout(SettingsDialog);
        rootLayout->setObjectName("rootLayout");
        accountsHeaderLabel = new QLabel(SettingsDialog);
        accountsHeaderLabel->setObjectName("accountsHeaderLabel");

        rootLayout->addWidget(accountsHeaderLabel);

        accountsList = new QListWidget(SettingsDialog);
        accountsList->setObjectName("accountsList");

        rootLayout->addWidget(accountsList);

        accountButtonsLayout = new QHBoxLayout();
        accountButtonsLayout->setObjectName("accountButtonsLayout");
        addAccountButton = new QPushButton(SettingsDialog);
        addAccountButton->setObjectName("addAccountButton");

        accountButtonsLayout->addWidget(addAccountButton);

        removeAccountButton = new QPushButton(SettingsDialog);
        removeAccountButton->setObjectName("removeAccountButton");

        accountButtonsLayout->addWidget(removeAccountButton);

        setDefaultButton = new QPushButton(SettingsDialog);
        setDefaultButton->setObjectName("setDefaultButton");

        accountButtonsLayout->addWidget(setDefaultButton);

        accountButtonsSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        accountButtonsLayout->addItem(accountButtonsSpacer);


        rootLayout->addLayout(accountButtonsLayout);

        settingsGroupBox = new QGroupBox(SettingsDialog);
        settingsGroupBox->setObjectName("settingsGroupBox");
        settingsFormLayout = new QFormLayout(settingsGroupBox);
        settingsFormLayout->setObjectName("settingsFormLayout");
        cacheTtlLabel = new QLabel(settingsGroupBox);
        cacheTtlLabel->setObjectName("cacheTtlLabel");

        settingsFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, cacheTtlLabel);

        cacheTtlSpin = new QSpinBox(settingsGroupBox);
        cacheTtlSpin->setObjectName("cacheTtlSpin");
        cacheTtlSpin->setMinimum(10);
        cacheTtlSpin->setMaximum(3600);
        cacheTtlSpin->setValue(60);

        settingsFormLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, cacheTtlSpin);

        languageLabel = new QLabel(settingsGroupBox);
        languageLabel->setObjectName("languageLabel");

        settingsFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, languageLabel);

        languageCombo = new QComboBox(settingsGroupBox);
        languageCombo->addItem(QString::fromUtf8("\320\243\320\272\321\200\320\260\321\227\320\275\321\201\321\214\320\272\320\260"));
        languageCombo->addItem(QString::fromUtf8("English"));
        languageCombo->setObjectName("languageCombo");

        settingsFormLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, languageCombo);

        logLevelLabel = new QLabel(settingsGroupBox);
        logLevelLabel->setObjectName("logLevelLabel");

        settingsFormLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, logLevelLabel);

        logLevelCombo = new QComboBox(settingsGroupBox);
        logLevelCombo->addItem(QString::fromUtf8("DEBUG"));
        logLevelCombo->addItem(QString::fromUtf8("INFO"));
        logLevelCombo->addItem(QString::fromUtf8("ERROR"));
        logLevelCombo->setObjectName("logLevelCombo");

        settingsFormLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, logLevelCombo);

        exportDocsCheck = new QCheckBox(settingsGroupBox);
        exportDocsCheck->setObjectName("exportDocsCheck");

        settingsFormLayout->setWidget(3, QFormLayout::ItemRole::SpanningRole, exportDocsCheck);

        exportSheetsCheck = new QCheckBox(settingsGroupBox);
        exportSheetsCheck->setObjectName("exportSheetsCheck");

        settingsFormLayout->setWidget(4, QFormLayout::ItemRole::SpanningRole, exportSheetsCheck);


        rootLayout->addWidget(settingsGroupBox);

        verticalSpacer = new QSpacerItem(20, 16, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        rootLayout->addItem(verticalSpacer);

        buttonBox = new QDialogButtonBox(SettingsDialog);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        rootLayout->addWidget(buttonBox);


        retranslateUi(SettingsDialog);

        QMetaObject::connectSlotsByName(SettingsDialog);
    } // setupUi

    void retranslateUi(QDialog *SettingsDialog)
    {
        SettingsDialog->setWindowTitle(QCoreApplication::translate("SettingsDialog", "Google Drive plugin settings", nullptr));
        accountsHeaderLabel->setText(QCoreApplication::translate("SettingsDialog", "Connected Google accounts:", nullptr));
        addAccountButton->setText(QCoreApplication::translate("SettingsDialog", "Add Account", nullptr));
        removeAccountButton->setText(QCoreApplication::translate("SettingsDialog", "Remove Account", nullptr));
        setDefaultButton->setText(QCoreApplication::translate("SettingsDialog", "Set as Default", nullptr));
        settingsGroupBox->setTitle(QCoreApplication::translate("SettingsDialog", "Plugin Settings", nullptr));
        cacheTtlLabel->setText(QCoreApplication::translate("SettingsDialog", "Cache TTL (seconds):", nullptr));
        languageLabel->setText(QCoreApplication::translate("SettingsDialog", "Language:", nullptr));

        logLevelLabel->setText(QCoreApplication::translate("SettingsDialog", "Log level:", nullptr));

        exportDocsCheck->setText(QCoreApplication::translate("SettingsDialog", "Export Google Docs as .docx (otherwise PDF)", nullptr));
        exportSheetsCheck->setText(QCoreApplication::translate("SettingsDialog", "Export Google Sheets as .xlsx (otherwise PDF)", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsDialog: public Ui_SettingsDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SETTINGS_DIALOG_H
