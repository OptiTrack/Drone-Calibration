#include "StreamingController.h"

StreamingController::StreamingController(QWidget* parent)
    : m_parent(parent)
{
    setupConnectionWidgets();
    setupTakeWidgets();
    setupSignalSlots();
}

QPushButton* StreamingController::getConnectButton()
{
    return connectionWidgets->connectButton;
}

void StreamingController::setIsRecording(bool recordStatus)
{
    isRecording = recordStatus;
}

void StreamingController::onConnectionSettingChange(int row, QString settingValue)
{
    // Update Connection Settings
    switch(row)
    {
        case 0: connectionSettings.serverIP = settingValue; break;
        case 1: connectionSettings.clientIP = settingValue; break;
        case 2: connectionSettings.connectionType = settingValue; break;
        case 3: connectionSettings.namingConvention = settingValue; break;
    }
}

void StreamingController::onConnectButtonClick(bool isChecked)
{
    if (isChecked){
        emit streamingConnect(connectionSettings, isRecording);
    } else {
        emit streamingDisconnect();
        emit streamLockedStatus(false);
    }
}

void StreamingController::onCommonTakeLoadButtonClick(bool isChecked, const QString fileName, const QString playSpeed)
{
    if (isChecked) {
        setTakeWidgetLoadState(commonTakeWidgets);
        emit loadCommonTake(fileName, playSpeed);
    } else {
        resetTakeWidgetState(commonTakeWidgets);
        emit streamLockedStatus(false);
    }
}

void StreamingController::onSavedTakeLoadButtonClick(bool isChecked, const QString fileName, const QString playSpeed)
{
    if (isChecked) {
        setTakeWidgetLoadState(savedTakeWidgets);
        emit loadSavedTake(fileName, playSpeed);
    } else {
        resetTakeWidgetState(savedTakeWidgets);
        emit streamLockedStatus(false);
    }
}

void StreamingController::onCommonTakeRunButtonClick()
{
    if (commonTakeWidgets->runButton->isChecked()) {
        startRunButtonState(commonTakeWidgets->runButton);
        emit runTake(isRecording);
    } else {
        resetTakeWidgetState(commonTakeWidgets);
        emit stopTake();
        emit streamLockedStatus(false);
    }
}

void StreamingController::onSavedTakeRunButtonClick()
{
    if (savedTakeWidgets->runButton->isChecked()) {
        startRunButtonState(savedTakeWidgets->runButton);
        emit runTake(isRecording);
    } else {
        resetTakeWidgetState(savedTakeWidgets);
        emit stopTake();
        emit streamLockedStatus(false);
    }
}

void StreamingController::onConnectionStatus(bool isConnected)
{
    if (isConnected) {
        setConnectionWidgetRunState();
        emit streamLockedStatus(true);
    } else {
        resetConnectionWidgetState();
        emit streamLockedStatus(false);
    }
}

void StreamingController::onCommonTakeReadyStatus(bool isReady)
{
    if (isReady) {
        setTakeWidgetRunState(commonTakeWidgets);
        emit streamLockedStatus(true);
    } else {
        resetTakeWidgetState(commonTakeWidgets);
        emit streamLockedStatus(false);
    }
}

void StreamingController::onSavedTakeReadyStatus(bool isReady)
{
    if (isReady) {
        setTakeWidgetRunState(savedTakeWidgets);
        emit streamLockedStatus(true);
    } else {
        resetTakeWidgetState(savedTakeWidgets);
        emit streamLockedStatus(false);
    }
}

void StreamingController::onNewSavedTake()
{
    savedTakeWidgets->listWidget->clear();
    populateSavedTakes();
}

void StreamingController::setupConnectionWidgets()
{
    connectionWidgets = uiFactory.createConnectionWidgets("Connection Settings", connectionSettings);
    addGroupBoxToUI(m_parent, connectionWidgets->groupBox);
    connectionSettingsTableWidget = connectionWidgets->tableWidget;
}

void StreamingController::setupTakeWidgets()
{
    commonTakeWidgets = uiFactory.createTakeWidgets("Common Takes");
    savedTakeWidgets = uiFactory.createTakeWidgets("Saved Takes");

    addGroupBoxToUI(m_parent, commonTakeWidgets->groupBox);
    addGroupBoxToUI(m_parent, savedTakeWidgets->groupBox);

    commonTakeWidgets->listWidget->addItems(fetchResourceFileNames("/json/src/assets/json/"));
    populateSavedTakes();
}

void StreamingController::setupSignalSlots()
{
    // Connect connectionSettingsTableWidget's cellChanged signals to onConnectionSettingChange slot
    connect(connectionSettingsTableWidget, &QTableWidget::cellChanged, this, [=](int row, int col) {
        QWidget* widget = connectionSettingsTableWidget->cellWidget(row, col);
        if (qobject_cast<QComboBox*>(widget)) return;

        QTableWidgetItem* item = connectionSettingsTableWidget->item(row, col);
        if (!item) return;

        QString settingValue = item->text();
        onConnectionSettingChange(row, settingValue);
    });

    // Connect connectionSettingsTableWidget's currentTextChanged signals to onConnectionSettingChange slot
    for (auto row = 0; row < connectionSettingsTableWidget->rowCount(); ++row) {
        if (auto comboBox = qobject_cast<QComboBox*>(connectionSettingsTableWidget->cellWidget(row, 0))) {
            connect(comboBox, &QComboBox::currentTextChanged, this, [=](const QString& settingValue) {
                onConnectionSettingChange(row, settingValue);
            });
        };
    };

    // Connect connectButton's clicked signal to onConnectButtonClick Slot
    connect(connectionWidgets->connectButton, &QPushButton::clicked, this, [this]() {
        bool isChecked = connectionWidgets->connectButton->isChecked();
        onConnectButtonClick(isChecked);
    });

    // Connect loadButton's clicked signal to onCommonTakeLoadButtonClick Slot
    connect(commonTakeWidgets->loadButton, &QPushButton::clicked, this, [this]() {
        bool isChecked = commonTakeWidgets->loadButton->isChecked();
        QListWidgetItem *item = commonTakeWidgets->listWidget->currentItem();
        QString playSpeed = commonTakeWidgets->playSpeed->currentText();
        if (item) {
            onCommonTakeLoadButtonClick(isChecked, item->text(), playSpeed);
        }
    });

    // Connect loadButton's clicked signal to onSavedTakeLoadButtonClick Slot
    connect(savedTakeWidgets->loadButton, &QPushButton::clicked, this, [this]() {
        bool isChecked = savedTakeWidgets->loadButton->isChecked();
        QListWidgetItem *item = savedTakeWidgets->listWidget->currentItem();
        QString playSpeed = savedTakeWidgets->playSpeed->currentText();
        if (item) {
            onSavedTakeLoadButtonClick(isChecked, item->text(), playSpeed);
        }
    });

    // Connect commonTakeWidget's runButton clicked signal to onCommonTakeRunButtonClick Slot
    connect(commonTakeWidgets->runButton, &QPushButton::clicked, this, &StreamingController::onCommonTakeRunButtonClick);

    // Connect savedTakeWidget's runButton clicked signal to onRunButtonClick Slot
    connect(savedTakeWidgets->runButton, &QPushButton::clicked, this, &StreamingController::onSavedTakeRunButtonClick);
}

void StreamingController::populateSavedTakes()
{
    savedTakeWidgets->listWidget->addItems(fetchSavedTakeFileNames());
}

void StreamingController::resetConnectionWidgetState()
{
    connectionWidgets->connectedStatus->setText("No");
    connectionWidgets->connectButton->setText("Connect");
    connectionWidgets->connectButton->setChecked(false);
    connectionSettingsTableWidget->setEnabled(true);
    enableGroupBoxWidgets(commonTakeWidgets->groupBox, true);
    enableGroupBoxWidgets(savedTakeWidgets->groupBox, true);
    commonTakeWidgets->runButton->setEnabled(false);
    savedTakeWidgets->runButton->setEnabled(false);
}

void StreamingController::setConnectionWidgetRunState()
{
    connectionWidgets->connectedStatus->setText("Yes");
    connectionWidgets->connectButton->setText("Disconnect");
    connectionSettingsTableWidget->setEnabled(false);
    enableGroupBoxWidgets(commonTakeWidgets->groupBox, false);
    enableGroupBoxWidgets(savedTakeWidgets->groupBox, false);
}

void StreamingController::resetTakeWidgetState(TakeWidgets *takeWidgets)
{
    takeWidgets->loadButton->setText("Load");
    takeWidgets->runButton->setText("Run");
    takeWidgets->listWidget->setEnabled(true);
    takeWidgets->playSpeed->setEnabled(true);
    takeWidgets->runButton->setEnabled(false);

    enableGroupBoxWidgets(connectionWidgets->groupBox, true);

    if (takeWidgets->name == "Common Takes") {
        enableGroupBoxWidgets(savedTakeWidgets->groupBox, true);
        savedTakeWidgets->runButton->setEnabled(false);
    } else if (takeWidgets->name == "Saved Takes") {
        enableGroupBoxWidgets(commonTakeWidgets->groupBox, true);
        commonTakeWidgets->runButton->setEnabled(false);
    }
}

void StreamingController::setTakeWidgetLoadState(TakeWidgets *takeWidgets)
{
    takeWidgets->listWidget->setEnabled(false);
    takeWidgets->playSpeed->setEnabled(false);
}

void StreamingController::setTakeWidgetRunState(TakeWidgets *takeWidgets)
{
    takeWidgets->loadButton->setText("Unload");
    takeWidgets->runButton->setEnabled(true);
    enableGroupBoxWidgets(connectionWidgets->groupBox, false);

    if (takeWidgets->name == "Common Takes") {
        enableGroupBoxWidgets(savedTakeWidgets->groupBox, false);
    } else if (takeWidgets->name == "Saved Takes") {
        enableGroupBoxWidgets(commonTakeWidgets->groupBox, false);
    }
}

void StreamingController::startRunButtonState(QPushButton *runButton)
{
    runButton->setText("Stop");
}
