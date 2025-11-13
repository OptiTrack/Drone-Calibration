#include "connection_controller.h"

ConnectionController::ConnectionController(QObject* parent)
    : QObject(parent)
{
}

void ConnectionController::startConnection(ConnectionSettings connectionSettings) {
    connection.setServerIP(connectionSettings.serverIP);
    connection.setClientIP(connectionSettings.clientIP);
    connection.setConnectionType(connectionSettings.connectionType);
    connection.setNamingConvention(connectionSettings.namingConvention);

    // Sets a callback in NatNetConnection to emit the framesUpdated() signal whenever new frame data is received.
    connection.setFrameUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            FrameData latestFrame = connection.getLatestFrame();
            emit framesUpdated(latestFrame);
        }, Qt::QueuedConnection);
    });

    // Sets a callback in NatNetConnection to emit the sendMaps() signal whenever new frame data is received.
    connection.setAssetUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            const std::unordered_map<int, std::string> rigidBodiesMap = connection.getRigidBodyIdToName();
            const std::unordered_map<int, std::string> skeletonsMap = connection.getSkeletonIdToName();
            const std::unordered_map<int, std::unordered_map<int, std::string>> bonesMap = connection.getBoneIdToName();
            emit sendMaps(rigidBodiesMap, skeletonsMap, bonesMap);
        }, Qt::QueuedConnection);
    });

    connection.connect();
    qDebug() << "ConnetionController: connect status signal sent" << connection.getConnectionStatus();
    emit connectionStatus(connection.getConnectionStatus());
}

void ConnectionController::stopConnection() {
    connection.disconnect();
    qDebug() << "ConnetionController: disconnect status signal sent" << connection.getConnectionStatus();
    emit connectionStatus(connection.getConnectionStatus());
}

const std::vector<FrameData>& ConnectionController::getFrames() const
{
    return connection.getFrames();
}

const std::unordered_map<int, std::string>& ConnectionController::getRigidBodyIdToName() const
{
    return connection.getRigidBodyIdToName();
}

const std::unordered_map<int, std::string>& ConnectionController::getSkeletonIdToName() const
{
    return connection.getSkeletonIdToName();
}

const std::unordered_map<int, std::unordered_map<int, std::string>>& ConnectionController::getBoneIdToName() const
{
    return connection.getBoneIdToName();
}

sDataDescriptions* ConnectionController::getDataDescriptions()
{
    return connection.getDataDescriptions();
}

void ConnectionController::replayFrame(FrameData frame)
{
    emit framesUpdated(frame);
    qDebug() << "ConnectionController: replay frame emit framesUpdated" << frame.frameNumber;
}