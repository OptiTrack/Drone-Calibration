#include "configurecontroller.h"

ConfigureController::ConfigureController(QWidget* parent)
    : m_parent(parent)
{
    setupSportsWidgets();
    setupAssetWidgets();
    setupSignalSlots();
}

void ConfigureController::setupSportSettings()
{
    sportsFile = loadJSON(sportFilePath);
    sportTypes = parseSportTypes(sportsFile);
    activeSport = sportTypes[0];

    sportsWidgets->sportTypes->addItems(sportTypes);
}

void ConfigureController::onSportSelectionChange(int sportIndex)
{
    activeSport = sportTypes.value(sportIndex);

    QJsonArray rigidMetricSettings = parseSportMetricSettings(sportsFile, activeSport, "rigidMetrics");
    QJsonArray bodyMetricSettings = parseSportMetricSettings(sportsFile, activeSport, "bodyMetrics");

    emit updatedMetricSettings(rigidMetricSettings, bodyMetricSettings);
}

void ConfigureController::onAssetSelectionChange(int row, QString assetValue)
{
    // Update Asset Selection
    switch(row)
    {
    case 0: assetSettings.skeleton = assetValue; break;
    case 1: assetSettings.rigidBody = assetValue; break;
    }

    emit assetSelected(assetSettings);
}

void ConfigureController::onSendAssets(const QMap<QString, int>& skeletons, const QMap<QString, int>& rigidBodies)
{
    if (assetWidgets->rigidBodyTypes->count() > 0) {
        assetWidgets->rigidBodyTypes->clear();
    }

    if (assetWidgets->skeletonTypes->count() > 0) {
        assetWidgets->skeletonTypes->clear();
    }

    addSkeletonAssets(skeletons);
    addRigidBodyAssets(rigidBodies);
}

void ConfigureController::setupSportsWidgets()
{
    sportsWidgets = uiFactory.createSportsWidgets("Sports");
    addGroupBoxToUI(m_parent, sportsWidgets->groupBox);
}

void ConfigureController::setupAssetWidgets()
{
    assetWidgets = uiFactory.createAssetWidgets("Assets");
    addGroupBoxToUI(m_parent, assetWidgets->groupBox);
}

void ConfigureController::setupSignalSlots()
{
    // Connect sportType changed signal to onSportSelectionChange Slot
    connect(sportsWidgets->sportTypes, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigureController::onSportSelectionChange);

    // Connect assetsTable's currentTextChanged signals to onAssetSelectionChange slot
    for (auto row = 0; row < assetWidgets->tableWidget->rowCount(); ++row) {
        if (auto comboBox = qobject_cast<QComboBox*>(assetWidgets->tableWidget->cellWidget(row, 0))) {
            connect(comboBox, &QComboBox::currentTextChanged, this, [=](const QString& assetValue) {
                onAssetSelectionChange(row, assetValue);
            });
        };
    };
}

void ConfigureController::addSkeletonAssets(const QMap<QString, int>& skeletons)
{
    for (auto it = skeletons.constBegin(); it != skeletons.constEnd(); ++it) {
        assetWidgets->skeletonTypes->addItem(it.key());
    }
}

void ConfigureController::addRigidBodyAssets(const QMap<QString, int>& rigidBodies)
{
    for (auto it = rigidBodies.constBegin(); it != rigidBodies.constEnd(); ++it) {
        assetWidgets->rigidBodyTypes->addItem(it.key());
    }
}
