#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QThread>
#include <QStyleFactory>
#include <QFile>
#include <qjsonobject.h>

#include "connection_controller.h"
#include "replay_controller.h"
#include "data_processor.h"
#include "./src/controllers/metricsmanager.h"
#include "./src/utils/fileutils.h"
#include "glwidget.h"

// Sets up the NatNet connection controller and thread.
// Returns a pointer to the created ConnectionController object.
ConnectionController* setupConnection()
{
    // Create controller and thread
    ConnectionController* controller = new ConnectionController;
    QThread* connectionThread = new QThread;

    // Move controller to the new thread
    controller->moveToThread(connectionThread);

    // Start the thread
    connectionThread->start();

    return controller;
}

// Sets up the DataProcessor and thread.
// Teturns a pointer to the created DataProcessor object.
DataProcessor* setupProcessor(const std::vector<FrameData>& frames)
{
    // Create processor and thread
    DataProcessor* processor = new DataProcessor(frames);
    QThread* dataThread = new QThread;

    // Move the controller to the new thread
    processor->moveToThread(dataThread);

    // Start the thread
    dataThread->start();

    return processor;
}

int main(int argc, char *argv[])
{
    // Create application
    QApplication a(argc, argv);
    MainWindow* w = new MainWindow();

    // Configure css
    QString styleSheet = loadStyleSheet(":/css/src/assets/css/client.css");
    a.setStyleSheet(styleSheet);

    // Display window
    w->show();

    // Set up connection
    ConnectionController* connectionController = setupConnection();

    // Set up replay controller
    ReplayController* replayController = new ReplayController();
    replayController->setOpenGLWidget(w->getOpenGLWidget());

    // Connect replay frame signal from ReplayController to ConnectionController 
    QObject::connect(replayController, &ReplayController::replayFrame,
        connectionController, &ConnectionController::replayFrame);

    // Set controller in main window for GLwidget
    w->setConnectionController(connectionController);

    // Get frame data list
    const std::vector<FrameData>& frames = connectionController->getFrames();

    // Set up data processor
    DataProcessor* processor = setupProcessor(frames);

    // Pass data processor into replay controller for access to name maps and frame data
    replayController->setDataProcessor(processor);

    // Connect new frame signal from ConnectionController to DataProcessor 
    QObject::connect(connectionController, &ConnectionController::sendMaps,
        processor, &DataProcessor::receiveMaps);

    // Connect new frame signal from ConnectionController to DataProcessor 
    QObject::connect(connectionController, &ConnectionController::framesUpdated,
        processor, &DataProcessor::onFramesUpdated);

    // Connect new metrics computed signal from RigidBodyMetrics to MainWindow
    // Fetch streamingController
    MetricsManager* rigidMetricsManager = w->getRigidMetricsManager();
    MetricsManager* bodyMetricsManager = w->getBodyMetricsManager();

    // Connect new metrics computed signal from RigidMetricsManager to MainWindow
    QObject::connect(processor, &DataProcessor::metricsComputed,
                     rigidMetricsManager, &MetricsManager::onMetricsComputed);

    // Connect new metrics computed signal from BodyMetricsManager to MainWindow
    QObject::connect(processor, &DataProcessor::metricsComputed,
                     bodyMetricsManager, &MetricsManager::onMetricsComputed);

    // Fetch streamingController
    StreamingController* streamingController = w->getStreamingController();
    ConfigureController* configureController = w->getConfigureController();

    // Connect load common take signal from StreamingController to ReplayController 
        QObject::connect(streamingController, &StreamingController::loadCommonTake,
            replayController, &ReplayController::loadCommonTake);
            
    // Connect load saved signal from StreamingController to ReplayController 
        QObject::connect(streamingController, &StreamingController::loadSavedTake,
            replayController, &ReplayController::loadSavedTake);

    // Connect replay frame signal from StreamingController to ReplayController 
        QObject::connect(replayController, &ReplayController::loadReplayMaps,
            processor, &DataProcessor::receiveMaps);

    // Connect load common take signal from ReplayController to StreamingController 
        QObject::connect(replayController, &ReplayController::commonTakeReady,
            streamingController, &StreamingController::onCommonTakeReadyStatus);

    // Connect load saved take signal from ReplayController to StreamingController 
        QObject::connect(replayController, &ReplayController::savedTakeReady,
            streamingController, &StreamingController::onSavedTakeReadyStatus);      

    // Connect run take signal from StreamingController to ReplayController 
        QObject::connect(streamingController, &StreamingController::runTake,
            replayController, &ReplayController::startReplay);  

    // Connect connection signal from StreamingController to ConnectionController
    QObject::connect(streamingController, &StreamingController::streamingConnect,
        connectionController, &ConnectionController::startConnection);

    // Connect disconnect signal from StreamingController to ConnectionController
    QObject::connect(streamingController, &StreamingController::streamingDisconnect,
                     connectionController, &ConnectionController::stopConnection);

    // Connect connection signal from StreamingController to DataProcessor
    QObject::connect(streamingController, &StreamingController::streamingConnect,
        processor, &DataProcessor::receiveNamingConvention);

    // Connect connection status signal from ConnectionController to StreamingController
    QObject::connect(connectionController, &ConnectionController::connectionStatus,
                     streamingController, &StreamingController::onConnectionStatus);

    // Connect asset send signal from DataProcessor to StreamingController
    QObject::connect(processor, &DataProcessor::sendAssets,
                     configureController, &ConfigureController::onSendAssets);

    // Connect asset selection signal from StreamingController to DataProcessor 
    QObject::connect(configureController, &ConfigureController::assetSelected,
        processor, &DataProcessor::receiveAssets);

    // Connect asset selection signal from StreamingController to DataProcessor 
    QObject::connect(configureController, &ConfigureController::assetSelected,
        w->getOpenGLWidget(), &GLWidget::selectAsset);

    // Connect metric settings signal from MainWindow to DataProcessor
    QObject::connect(configureController, &ConfigureController::updatedMetricSettings,
        processor, &DataProcessor::receiveMetricSettings);

    // Connect disconnect signal from StreamingController to ReplayController 
    QObject::connect(streamingController, &StreamingController::streamingDisconnect,
        replayController, &ReplayController::saveStream);

    // Connect connection signal from StreamingController to ReplayController 
    QObject::connect(streamingController, &StreamingController::streamingConnect,
        replayController, &ReplayController::recordStream);

    // Connect run take signal from StreamingController to ReplayController 
    QObject::connect(streamingController, &StreamingController::runTake,
        replayController, &ReplayController::recordReplay);

    // Connect stop take signal from StreamingController to ReplayController 
    QObject::connect(streamingController, &StreamingController::stopTake,
        replayController, &ReplayController::saveReplay);

    // Connect new take signal from ReplayController to StreamingController 
    QObject::connect(replayController, &ReplayController::newSavedTake,
        streamingController, &StreamingController::onNewSavedTake);

    return a.exec();
}
