#include "uifactory.h"

UiFactory::UiFactory() {}

ConnectionWidgets* UiFactory::createConnectionWidgets(const QString name, ConnectionSettings connectionSettings)
{
    // Create new connectionWidgets structure & layout
    ConnectionWidgets* connectionWidgets = new ConnectionWidgets();
    QVBoxLayout *layout = new QVBoxLayout();

    // Set name
    connectionWidgets->name = name;

    // Set groupBox Settings
    connectionWidgets->groupBox->setTitle(name);
    connectionWidgets->groupBox->setCheckable(true);

    // Update tableWidget Settings
    connectionWidgets->tableWidget->setColumnCount(1);
    connectionWidgets->tableWidget->setRowCount(5);
    connectionWidgets->tableWidget->horizontalHeader()->setVisible(false);
    connectionWidgets->tableWidget->verticalHeader()->setVisible(true);
    connectionWidgets->tableWidget->horizontalHeader()->setStretchLastSection(true);
    connectionWidgets->tableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    connectionWidgets->tableWidget->setVerticalHeaderLabels({"Server IP:", "Client IP:", "Connection Type:", "Naming Convention:", "Connected:"});
    connectionWidgets->tableWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connectionWidgets->tableWidget->setSelectionMode(QAbstractItemView::NoSelection);
    connectionWidgets->tableWidget->setEditTriggers(QAbstractItemView::EditTrigger::AllEditTriggers);

    // Update connectionWidgets Settings
    connectionWidgets->serverIP->setText(connectionSettings.serverIP);
    connectionWidgets->clientIP->setText(connectionSettings.clientIP);
    connectionWidgets->connectionTypes->addItems({"Multicast", "Unicast"});
    connectionWidgets->connectionTypes->setProperty("flat", true);
    connectionWidgets->namingConventions->addItems({"Motive", "FBX", "BVH", "UnrealEngine"});
    connectionWidgets->namingConventions->setCurrentIndex(1);
    connectionWidgets->namingConventions->setProperty("flat", true);
    connectionWidgets->connectedStatus->setText("No");
    connectionWidgets->connectedStatus->setFlags(connectionWidgets->connectedStatus->flags() & ~Qt::ItemIsEditable);
    connectionWidgets->connectButton->setCheckable(true);
    connectionWidgets->connectButton->setText("Connect");
    connectionWidgets->connectButton->setProperty("connect", true);

    // Add connectionWidgets into tableWidget
    connectionWidgets->tableWidget->setItem(0, 0, connectionWidgets->serverIP);
    connectionWidgets->tableWidget->setItem(1, 0, connectionWidgets->clientIP);
    connectionWidgets->tableWidget->setCellWidget(2, 0, connectionWidgets->connectionTypes);
    connectionWidgets->tableWidget->setCellWidget(3, 0, connectionWidgets->namingConventions);
    connectionWidgets->tableWidget->setItem(4, 0, connectionWidgets->connectedStatus);

    // Add widgets into layout
    layout->addWidget(connectionWidgets->tableWidget, 0);
    layout->addWidget(connectionWidgets->connectButton);

    // Set groupBox layout
    connectionWidgets->groupBox->setLayout(layout);

    // Return pointer to connectionWidgets
    return connectionWidgets;
}

TakeWidgets* UiFactory::createTakeWidgets(const QString name)
{
    // Create new takeWidgets structure & layout
    TakeWidgets* takeWidgets = new TakeWidgets();
    QVBoxLayout *layout = new QVBoxLayout();

    // Create takeSettings Container
    QWidget *takeSettingsContainer = new QWidget();
    QHBoxLayout *settingsLayout = new QHBoxLayout();
    takeSettingsContainer->setLayout(settingsLayout);
    takeSettingsContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    settingsLayout->setContentsMargins(0, 0, 0, 0);

    // Set name
    takeWidgets->name = name;

    // Set groupBox Settings
    takeWidgets->groupBox->setTitle(name);
    takeWidgets->groupBox->setCheckable(true);

    // Update takeWidgets Settings
    takeWidgets->playSpeed->addItems({"100%", "50%", "25%", "12.5%", "10%", "5%"});
    takeWidgets->playSpeed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    takeWidgets->playSpeed->setProperty("simple", true);
    takeWidgets->loadButton->setText("Load");
    takeWidgets->loadButton->setCheckable(true);
    takeWidgets->loadButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    takeWidgets->runButton->setText("Run");
    takeWidgets->runButton->setCheckable(true);
    takeWidgets->runButton->setEnabled(false);
    takeWidgets->runButton->setProperty("connect", true);

    // Add widgets into layout
    settingsLayout->addWidget(takeWidgets->playSpeed);
    settingsLayout->addWidget(takeWidgets->loadButton);
    settingsLayout->setStretch(0, 1);
    settingsLayout->setStretch(1, 1);
    layout->addWidget(takeWidgets->listWidget);
    layout->addWidget(takeSettingsContainer);
    layout->addWidget(takeWidgets->runButton);

    // Set groupBox layout
    takeWidgets->groupBox->setLayout(layout);

    // Return pointer to connectionWidgets
    return takeWidgets;
}

SportsWidgets* UiFactory::createSportsWidgets(const QString name)
{
    // Create new sportsWidgets structure & layout
    SportsWidgets* sportsWidgets = new SportsWidgets();
    QVBoxLayout *layout = new QVBoxLayout();

    // Set name
    sportsWidgets->name = name;

    // Set groupBox Settings
    sportsWidgets->groupBox->setTitle(name);
    sportsWidgets->groupBox->setCheckable(true);

    // Update sportsWidgets Settings
    sportsWidgets->sportTypes->setProperty("simple", true);

    // Add widgets into layout
    layout->addWidget(sportsWidgets->sportTypes);

    // Set groupBox layout
    sportsWidgets->groupBox->setLayout(layout);

    // Return pointer to sportsWidgets
    return sportsWidgets;
}
AssetWidgets* UiFactory::createAssetWidgets(const QString name)
{
    // Create new assetWidgets structure & layout
    AssetWidgets* assetWidgets = new AssetWidgets();
    QVBoxLayout *layout = new QVBoxLayout();

    // Set name
    assetWidgets->name = name;

    // Set groupBox Settings
    assetWidgets->groupBox->setTitle(name);
    assetWidgets->groupBox->setCheckable(true);

    // Update tableWidget Settings
    assetWidgets->tableWidget->setColumnCount(1);
    assetWidgets->tableWidget->setRowCount(2);
    assetWidgets->tableWidget->horizontalHeader()->setVisible(false);
    assetWidgets->tableWidget->verticalHeader()->setVisible(true);
    assetWidgets->tableWidget->horizontalHeader()->setStretchLastSection(true);
    assetWidgets->tableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    assetWidgets->tableWidget->setVerticalHeaderLabels({"Skeleton:", "Bat:"});
    assetWidgets->tableWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    assetWidgets->tableWidget->setSelectionMode(QAbstractItemView::NoSelection);
    assetWidgets->tableWidget->setEditTriggers(QAbstractItemView::EditTrigger::AllEditTriggers);

    // Update assetWidgets Settings
    assetWidgets->skeletonTypes->setProperty("flat", true);
    assetWidgets->rigidBodyTypes->setProperty("flat", true);

    // Add connectionWidgets into tableWidget
    assetWidgets->tableWidget->setCellWidget(0, 0, assetWidgets->skeletonTypes);
    assetWidgets->tableWidget->setCellWidget(1, 0, assetWidgets->rigidBodyTypes);

    // Add widgets into layout
    layout->addWidget(assetWidgets->tableWidget, 0);

    // Set groupBox layout
    assetWidgets->groupBox->setLayout(layout);

    // Return pointer to assetWidgets
    return assetWidgets;
}

MetricWidgets* UiFactory::createMetricWidgets(const QString name, const QString units, QVector<QString> labels, QVector<QString> descriptions, QVector<bool> graphs) {

    // Create new metricWidgets structure & layout
    MetricWidgets *metricWidgets = new MetricWidgets();
    QVBoxLayout *layout = new QVBoxLayout();

    // Set name & units
    metricWidgets->name = name;
    metricWidgets->units = units;

    // Set groupBox Settings
    metricWidgets->groupBox->setTitle(name);
    metricWidgets->groupBox->setCheckable(true);

    for (int i = 0; i < labels.count(); ++i) {
        QLabel *dataLabel = new QLabel();
        dataLabel->setObjectName(labels[i] + "DataLabel");
        dataLabel->setText("- " + units);
        dataLabel->setAlignment(Qt::AlignRight);

        layout->addWidget(dataLabel);
        metricWidgets->dataLabels->append(dataLabel);

        QString description = descriptions.value(i);
        if (!description.isEmpty()) {
            QLabel *descriptionLabel = new QLabel();
            descriptionLabel->setObjectName(labels[i] + "DescriptionLabel");
            descriptionLabel->setText(description);
            descriptionLabel->setAlignment(Qt::AlignRight);

            layout->addWidget(descriptionLabel);
            metricWidgets->descriptionLabels->append(descriptionLabel);
        }

        if (graphs[i]) {
            GraphWidget *metricGraph = new GraphWidget(metricWidgets->groupBox);
            metricGraph->setObjectName(labels[i] + "Graph");
            metricGraph->setMinimumHeight(100);

            layout->addWidget(metricGraph);
            metricWidgets->metricGraphs->append(metricGraph);
        } else {
            metricWidgets->metricGraphs->append(nullptr);
        }
    }

    metricWidgets->groupBox->setLayout(layout);

    return metricWidgets;
}
