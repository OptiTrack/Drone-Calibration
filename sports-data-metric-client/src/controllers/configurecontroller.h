#ifndef CONFIGURECONTROLLER_H
#define CONFIGURECONTROLLER_H

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
#include "uifactory.h"

#pragma once

struct AssetSettings {
    QString skeleton;
    QString rigidBody;
};

class ConfigureController : public QObject {
    Q_OBJECT

public:
    ConfigureController(QWidget* parent);
    AssetWidgets getAssetWidgets();
    void setupSportSettings();

public slots:
    void onSportSelectionChange(int sportIndex);
    void onAssetSelectionChange(int row, QString assetValue);
    void onSendAssets(const QMap<QString, int>& skeletons, const QMap<QString, int>& rigidBodies);

signals:
    void updatedMetricSettings(QJsonArray rigidMetricSettings, QJsonArray bodyMetricSettings);
    void assetSelected(AssetSettings assetSettings);

private:
    QWidget* m_parent;
    UiFactory uiFactory;

    QString sportFilePath = ":/config/src/config/sports.json";
    QJsonObject sportsFile;
    QStringList sportTypes;
    QString activeSport;
    AssetSettings assetSettings;

    SportsWidgets *sportsWidgets = nullptr;
    AssetWidgets *assetWidgets = nullptr;

    void setupSportsWidgets();
    void setupAssetWidgets();

    void setupSignalSlots();
    void addSkeletonAssets(const QMap<QString, int>& skeletons);
    void addRigidBodyAssets(const QMap<QString, int>& rigidBodies);
};

#endif // CONFIGURECONTROLLER_H
