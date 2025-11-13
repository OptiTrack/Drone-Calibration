#ifndef STREAMINGCONTROLLER_H
#define STREAMINGCONTROLLER_H


#include <QComboBox>
#include <QTableWidgetItem>
#include <QMap>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QGroupBox>

#include "settings.h"
#include "uifactory.h"
#include "toggles.h"
#include "./src/utils/uiutils.h"
#include "./src/utils/fileutils.h"

#pragma once

class StreamingController : public QObject {
    Q_OBJECT

public:
    StreamingController(QWidget* parent);
    QPushButton* getConnectButton();
    void setIsRecording(bool recordStatus);

public slots:
    void onConnectionSettingChange(int row, QString settingValue);

    void onConnectButtonClick(bool isChecked);
    void onCommonTakeLoadButtonClick(bool isChecked, const QString fileName, const QString playSpeed);
    void onSavedTakeLoadButtonClick(bool isChecked, const QString fileName, const QString playSpeed);
    void onCommonTakeRunButtonClick();
    void onSavedTakeRunButtonClick();

    void onConnectionStatus(bool isConnected);
    void onCommonTakeReadyStatus(bool isReady);
    void onSavedTakeReadyStatus(bool isReady);
    void onNewSavedTake();

signals:
    void streamLockedStatus(bool isLocked);
    void streamingConnect(ConnectionSettings connectionSettings, bool isRecording);
    void streamingDisconnect();
    void loadCommonTake(const QString fileName, const QString playSpeed);
    void loadSavedTake(const QString fileName, const QString playSpeed);
    void runTake(bool isRecording);
    void stopTake();

private:
    QWidget* m_parent;
    UiFactory uiFactory;
    QTableWidget *connectionSettingsTableWidget = nullptr;
    ConnectionWidgets *connectionWidgets = nullptr;
    TakeWidgets *commonTakeWidgets = nullptr;
    TakeWidgets *savedTakeWidgets = nullptr;
    ConnectionSettings connectionSettings;

    bool isRecording = false;

    void setupConnectionWidgets();
    void setupTakeWidgets();
    void setupSignalSlots();
    void populateSavedTakes();

    void resetConnectionWidgetState();
    void setConnectionWidgetRunState();

    void resetTakeWidgetState(TakeWidgets *takeWidgets);
    void setTakeWidgetLoadState(TakeWidgets *takeWidgets);
    void setTakeWidgetRunState(TakeWidgets *takeWidgets);
    void startRunButtonState(QPushButton *runButton);
};

#endif // STREAMINGCONTROLLER_H
