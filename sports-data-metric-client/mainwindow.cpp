#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "configurecontroller.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    qDebug() << "Running UI Setup";
    ui->setupUi(this);

    setupToggles();
    setupStreamingController();
    setupConfigureController();
    setupMetricsManager();
    setupReportGenerator();
    setupSignalSlots();
    setupSportSettings();
}

MainWindow::~MainWindow()
{
    delete ui;
}

StreamingController* MainWindow::getStreamingController()
{
    return streamingController;
}

ConfigureController* MainWindow::getConfigureController()
{
    return configureController;
}


MetricsManager* MainWindow::getRigidMetricsManager()
{
    return rigidMetricsManager;
}

MetricsManager* MainWindow::getBodyMetricsManager()
{
    return bodyMetricsManager;
}

void MainWindow::setConnectionController(ConnectionController* controller)
{
    m_controller = controller;
    ui->openGLWidget->setController(m_controller);
}

void MainWindow::setupToggles()
{
    // Streaming Tab Settings
    TabToggle streamingTab = {
        ui->streamingToolButton,
        ui->streamingTabWidget,
        QIcon(":/small-icons/src/assets/icons/Small/Streaming/Streaming-Off.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Streaming/Streaming-On.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Streaming/Streaming-Disabled.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Streaming/Streaming-Active.svg"),
    };

    // Configure Tab Settings
    TabToggle configureTab = {
        ui->configureToolButton,
        ui->configureTabWidget,
        QIcon(":/small-icons/src/assets/icons/Small/Actions/Actions-Off.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Actions/Actions-On.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Actions/Actions-Disabled.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Actions/Actions-Active.svg"),
    };

    // Rigid Quick Tab Settings
    TabToggle rigidQuickTab = {
        ui->rigidQuickToolButton,
        ui->rigidQuickTabWidget,
        QIcon(":/small-icons/src/assets/icons/Small/Rigid Body/Rigid-Body-Off.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Rigid Body/Rigid-Body-On.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Rigid Body/Rigid-Body-Disabled.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Rigid Body/Rigid-Body-Active.svg"),
    };

    // Body Quick Tab Settings
    TabToggle bodyQuickTab = {
        ui->bodyQuickToolButton,
        ui->bodyQuickTabWidget,
        QIcon(":/small-icons/src/assets/icons/Small/Speed/Speed-Off.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Speed/Speed-On.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Speed/Speed-Disabled.svg"),
        QIcon(":/small-icons/src/assets/icons/Small/Speed/Speed-Active.svg"),
    };

    // Left pane Toggle List
    QList<TabToggle> leftPaneToggles {
        streamingTab,
        configureTab
    };

    // Right Pane Toggle List
    QList<TabToggle> rightPaneToggles {
        rigidQuickTab,
        bodyQuickTab
    };

    // Setup Toggles
    setupTabToggles(ui->leftTabWidget, leftPaneToggles);
    setupTabToggles(ui->rightTabWidget, rightPaneToggles);
}

void MainWindow::setupStreamingController()
{
    streamingController = new StreamingController(ui->streamingLayoutWidget);
}

void MainWindow::setupConfigureController()
{
    configureController = new ConfigureController(ui->configureLayoutWidget);
}

void MainWindow::setupMetricsManager()
{
    // Create Metrics Manager
    rigidMetricsManager = new MetricsManager(ui->rigidQuickLayoutWidget, "rigidMetricsManager");
    bodyMetricsManager = new MetricsManager(ui->bodyQuickLayoutWidget, "bodyMetricsManager");
}

void MainWindow::setupReportGenerator()
{
    reportGenerator = new ReportGenerator();
}

void MainWindow::setupSignalSlots()
{
    // Connect recordButtons's clicked signal to streamingController's setIsRecording function
    connect(ui->recordToolButton, &QPushButton::clicked, this, [=]() {
        bool recordStatus = ui->recordToolButton->isChecked();
        streamingController->setIsRecording(recordStatus);
    });

    // Define Controller Widgets
    QPushButton *connectButton = streamingController->getConnectButton();

    // Connect connectButton's clicked signal to reportGenerator's printMetricsReport function
    connect(connectButton, &QPushButton::clicked, this, [=]() {
        bool isStreamingDisconnected = !connectButton->isChecked();
        bool isExportChecked = ui->exportToolButton->isChecked();
        if (isStreamingDisconnected && isExportChecked) {
            reportGenerator->printMetricsReport();
        }
    });

    // Connect configureController's updatedMetricSettings signal to rigidMetricsManager's onUpdatedMetricSettings slot
    connect(configureController, &ConfigureController::updatedMetricSettings, rigidMetricsManager, &MetricsManager::onUpdatedMetricSettings);

    // Connect configureController's updatedMetricSettings signal to bodyMetricsManager's onUpdatedMetricSettings slot
    connect(configureController, &ConfigureController::updatedMetricSettings, bodyMetricsManager, &MetricsManager::onUpdatedMetricSettings);

    // Connect streamingController's streamLockedStatus signal to update mainWindows's lockLabel icon
    connect(streamingController, &StreamingController::streamLockedStatus, this, [=](bool isLocked) {
        QString iconPath = isLocked
            ? ":/small-icons/src/assets/icons/Small/Lock/Lock-On.svg"
            : ":/small-icons/src/assets/icons/Small/Lock/Lock-Broken-Active.svg";
        QPixmap iconPixmap(iconPath);
        ui->lockLabel->setPixmap(iconPixmap);
    });
}

void MainWindow::setupSportSettings()
{
    configureController->setupSportSettings();
}

GLWidget* MainWindow::getOpenGLWidget()
{
    return ui->openGLWidget;
}
