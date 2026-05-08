#include "dronecontroller.h"
#include "../network/voxlconnection.h"
#include "../network/voxlmapperclient.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>
#include <QUuid>
#include <QVector3D>
#include <QtMath>
#include <QDir>
#include <algorithm>

DroneController::DroneController(QObject *parent)
    : QObject(parent)
    , m_voxlConnection(nullptr)
    , m_mapperClient(nullptr)
    , m_connected(false)
    , m_droneHost("192.168.1.10")
    , m_dronePort(14550)
    , m_heartbeatTimer(nullptr)
    , m_statusUpdateTimer(nullptr)
    , m_silMode(false)
    , m_silHost("127.0.0.1")
    , m_silPort(14550)
    , m_currentMissionItem(0)
    , m_missionActive(false)
    , m_missionPaused(false)
    , m_mapperMissionState(MapperMissionState::Idle)
    , m_mapperMissionTimer(nullptr)
    , m_haveMapperPose(false)
    , m_resumeMissionItem(0)
    , m_pendingMapperMapCommand(PendingMapperMapCommand::None)
    , m_mapperPortalWatchdog(nullptr)
    , m_manualControlActive(false)
    , m_manualControlTimer(nullptr)
{
    // Initialize status
    m_currentStatus.connected = false;
    m_currentStatus.batteryPercentage = 0.0f;
    m_currentStatus.batteryVoltage = 0.0f;
    m_currentStatus.flightMode = "--";
    m_currentStatus.armed = false;
    m_currentStatus.gpsLock = false;
    m_currentStatus.gpsNumSats = 0;
    m_currentStatus.altitude = 0.0f;
    m_currentStatus.groundSpeed = 0.0f;
    m_currentStatus.verticalSpeed = 0.0f;
    m_currentStatus.position = QVector3D(0, 0, 0);
    m_currentStatus.velocity = QVector3D(0, 0, 0);
    m_currentStatus.attitude = QVector3D(0, 0, 0);
    m_currentStatus.systemStatus = "DISCONNECTED";
    
    // Initialize mission
    m_currentMission.id = "";
    m_currentMission.name = "";
    m_currentMission.uploaded = false;
    
    initializeConnection();
}

DroneController::~DroneController()
{
    disconnectFromDrone();
}

void DroneController::initializeConnection()
{
    // Create VOXL connection
    m_voxlConnection = new VOXLConnection(this);
    m_mapperClient = new VOXLMapperClient(this);
    
    // Connect signals
    connect(m_voxlConnection, &VOXLConnection::connected,
            this, [this]() { onVOXLConnectionStatusChanged(true); });
    connect(m_voxlConnection, &VOXLConnection::disconnected,
            this, [this]() { onVOXLConnectionStatusChanged(false); });
    connect(m_voxlConnection, &VOXLConnection::dataReceived,
            this, &DroneController::onVOXLDataReceived);
    connect(m_voxlConnection, &VOXLConnection::errorOccurred,
            this, &DroneController::onVOXLError);
    connect(m_voxlConnection, &VOXLConnection::mapperBundleDownloadFinished,
            this, &DroneController::mapperBundleDownloadFinished);
    connect(m_voxlConnection, &VOXLConnection::mapperBundleUploadFinished,
            this, &DroneController::onMapperBundleUploadForRestoreFinished);
    connect(m_mapperClient, &VOXLMapperClient::poseReceived,
            this, &DroneController::onMapperPoseReceived);
    connect(m_mapperClient, &VOXLMapperClient::pathRenderReceived,
            this, &DroneController::onMapperRenderReceived);
    connect(m_mapperClient, &VOXLMapperClient::meshRenderReceived,
            this, &DroneController::onMapperMeshReceived);
    connect(m_mapperClient, &VOXLMapperClient::meshConnectedChanged,
            this, &DroneController::onMapperMeshConnectedChanged);
    connect(m_mapperClient, &VOXLMapperClient::errorOccurred,
            this, [this](const QString &error) { emit errorOccurred(QString("VOXL Mapper: %1").arg(error)); });
    connect(m_mapperClient, &VOXLMapperClient::statusChanged,
            this, &DroneController::messageReceived);
    
    // Set up timers
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(1000); // 1 Hz heartbeat
    connect(m_heartbeatTimer, &QTimer::timeout, this, &DroneController::onHeartbeatTimer);
    
    m_statusUpdateTimer = new QTimer(this);
    m_statusUpdateTimer->setInterval(100); // 10 Hz status updates
    connect(m_statusUpdateTimer, &QTimer::timeout, this, &DroneController::onStatusUpdateTimer);
    m_mapperMissionTimer = new QTimer(this);
    m_mapperMissionTimer->setInterval(200);
    connect(m_mapperMissionTimer, &QTimer::timeout, this, &DroneController::onMapperTick);
    
    // Manual control timer
    m_manualControlTimer = new QTimer(this);
    m_manualControlTimer->setInterval(50); // 20 Hz manual control
}

bool DroneController::connectToDrone(const QString &host, int port)
{
    m_droneHost = host;
    m_dronePort = port;
    
    QString connectHost = m_silMode ? m_silHost : host;
    int connectPort = m_silMode ? m_silPort : port;
    
    emit messageReceived(QString("Connecting to drone at %1:%2%3")
                        .arg(connectHost)
                        .arg(connectPort)
                        .arg(m_silMode ? " (SIL Mode)" : ""));
    
    // Always tell VOXLConnection the real drone host (not SIL localhost)
    // so that SCP uploads and the runner REST API target the actual VOXL 2.
    m_voxlConnection->setVoxlHost(host);

    bool success = m_voxlConnection->connectToVOXL(connectHost, connectPort, VOXLConnection::UDP_CONNECTION);
    
    if (success) {
        emit messageReceived("Connection initiated...");
        if (!m_silMode && m_mapperClient) {
            m_mapperClient->connectToMapper(host, 80);
            stopMapperPortalWatchdog();
            m_mapperPortalWatchdog = new QTimer(this);
            m_mapperPortalWatchdog->setSingleShot(true);
            m_mapperPortalWatchdog->setInterval(8000);
            connect(m_mapperPortalWatchdog, &QTimer::timeout, this, &DroneController::onMapperPortalWatchdogTimeout);
            m_mapperPortalWatchdog->start();
        }
    } else {
        emit errorOccurred("Failed to initiate connection");
    }
    
    return success;
}

void DroneController::disconnectFromDrone()
{
    stopMapperPortalWatchdog();
    m_pendingBundleRestoreRemote.clear();
    if (m_voxlConnection) {
        m_voxlConnection->disconnect();
    }
    if (m_mapperClient && m_mapperClient->isConnected()) {
        m_mapperClient->stopFollowing();
        m_mapperClient->disconnectFromMapper();
    }
    if (m_mapperMissionTimer) {
        m_mapperMissionTimer->stop();
    }
    
    updateConnectionStatus(false);
    emit messageReceived("Disconnected from drone");
}

bool DroneController::mapperPoseAvailableForPlanner(QVector3D &logicalOut, float &yawDegOut) const
{
    if (!m_haveMapperPose)
        return false;
    logicalOut = m_currentStatus.position;
    yawDegOut = m_currentStatus.attitude.z();
    return true;
}

void DroneController::releaseMapperTrajectoryToVehicle()
{
    if (m_mapperClient && m_mapperClient->isConnected())
        m_mapperClient->stopFollowing();
    if (m_mapperMissionTimer && m_mapperMissionTimer->isActive())
        m_mapperMissionTimer->stop();
    m_mapperMissionState = MapperMissionState::Idle;
    m_missionActive = false;
    m_missionPaused = false;
}

void DroneController::updateConnectionStatus(bool connected)
{
    if (m_connected != connected) {
        m_connected = connected;
        m_currentStatus.connected = connected;
        
        if (connected) {
            m_heartbeatTimer->start();
            m_statusUpdateTimer->start();
        } else {
            m_heartbeatTimer->stop();
            m_statusUpdateTimer->stop();
            if (m_manualControlTimer->isActive()) {
                m_manualControlTimer->stop();
            }
            m_currentStatus.batteryPercentage = 0.0f;
            m_currentStatus.batteryVoltage = 0.0f;
            m_currentStatus.flightMode = "--";
            m_currentStatus.armed = false;
            m_currentStatus.gpsLock = false;
            m_currentStatus.gpsNumSats = 0;
            m_currentStatus.altitude = 0.0f;
            m_currentStatus.groundSpeed = 0.0f;
            m_currentStatus.verticalSpeed = 0.0f;
            m_currentStatus.position = QVector3D(0, 0, 0);
            m_currentStatus.velocity = QVector3D(0, 0, 0);
            m_currentStatus.attitude = QVector3D(0, 0, 0);
            m_currentStatus.lastHeartbeat.clear();
            m_currentStatus.systemStatus = "DISCONNECTED";
            m_missionActive = false;
            m_missionPaused = false;
            m_mapperMissionState = MapperMissionState::Idle;
            m_haveMapperPose = false;
        }
        
        emit connectionStatusChanged(connected);
        emit statusUpdated(m_currentStatus);
    }
}

void DroneController::armDrone(bool arm)
{
    if (!m_connected) {
        emit errorOccurred("Cannot change arm state: Not connected to drone");
        return;
    }

    sendCommand("arm_disarm", QJsonObject{{"arm", arm}});
    m_currentStatus.armed = arm;
    emit statusUpdated(m_currentStatus);
    emit messageReceived(arm ? "Arm command sent" : "Disarm command sent");
}

void DroneController::takeoff(float altitude)
{
    if (!m_connected) {
        emit errorOccurred("Cannot takeoff: Not connected to drone");
        return;
    }

    sendCommand("takeoff", QJsonObject{{"altitude", altitude}});
    emit messageReceived(QString("Takeoff command sent (altitude %1 m)").arg(altitude, 0, 'f', 1));
}

void DroneController::land()
{
    if (!m_connected) {
        emit errorOccurred("Cannot land: Not connected to drone");
        return;
    }

    const bool hadMapperRun = m_missionActive || m_mapperMissionState != MapperMissionState::Idle;
    releaseMapperTrajectoryToVehicle();
    sendCommand("land");
    if (hadMapperRun)
        emit missionStatusChanged(QStringLiteral("VOXL path following stopped — handing off to PX4 Land"));
    emit messageReceived(QStringLiteral("Land command sent"));
}

void DroneController::returnToLaunch()
{
    if (!m_connected) {
        emit errorOccurred("Cannot return to launch: Not connected to drone");
        return;
    }

    const bool hadMapperRun = m_missionActive || m_mapperMissionState != MapperMissionState::Idle;
    releaseMapperTrajectoryToVehicle();
    sendCommand("return_to_launch");
    if (hadMapperRun)
        emit missionStatusChanged(QStringLiteral("VOXL path following stopped — handing off to PX4 RTL"));
    emit messageReceived(QStringLiteral("Return-to-launch command sent"));
}

void DroneController::forceDisarm()
{
    if (!m_connected) {
        emit errorOccurred("Cannot force disarm: Not connected to drone");
        return;
    }

    releaseMapperTrajectoryToVehicle();
    sendCommand("force_disarm");
    emit missionStatusChanged(QStringLiteral("Force disarm (kill motors) requested"));
    emit messageReceived(QStringLiteral("Force disarm command sent (MAVLink force-disarm)"));
}

void DroneController::flightTermination()
{
    if (!m_connected) {
        emit errorOccurred("Cannot send flight termination: Not connected to drone");
        return;
    }

    releaseMapperTrajectoryToVehicle();
    sendCommand("flight_termination");
    emit missionStatusChanged(QStringLiteral("Flight termination command sent (PX4; may require reboot)"));
    emit messageReceived(QStringLiteral("Flight termination sent — check PX4 CBRK_FLIGHTTERM / docs"));
}

void DroneController::sendCommand(const QString &command, const QJsonObject &params)
{
    if (m_voxlConnection && m_connected) {
        m_voxlConnection->sendCommand(command, params);
    }
}

void DroneController::onHeartbeatTimer()
{
    if (m_voxlConnection && m_connected) {
        // Send heartbeat to maintain connection
        sendCommand("heartbeat");
    }
}

void DroneController::onStatusUpdateTimer()
{
    if (m_connected) {
        // Request status updates
        if (m_voxlConnection) {
            m_voxlConnection->requestStatus();
        }
    }
}

void DroneController::onVOXLDataReceived(const QJsonObject &data)
{
    QString messageType = data["type"].toString();
    
    if (messageType == "status") {
        QJsonObject statusData = data["data"].toObject();

        if (statusData.contains("battery")) {
            QJsonObject battery = statusData["battery"].toObject();
            m_currentStatus.batteryPercentage = battery["percentage"].toDouble();
            m_currentStatus.batteryVoltage = battery["voltage"].toDouble();
        }

        if (statusData.contains("batteryPercentage")) {
            m_currentStatus.batteryPercentage = static_cast<float>(statusData["batteryPercentage"].toDouble());
        }
        if (statusData.contains("batteryVoltage")) {
            m_currentStatus.batteryVoltage = static_cast<float>(statusData["batteryVoltage"].toDouble());
        }
        if (statusData.contains("flightMode")) {
            m_currentStatus.flightMode = statusData["flightMode"].toString();
        }
        if (statusData.contains("armed")) {
            m_currentStatus.armed = statusData["armed"].toBool();
        }
        if (statusData.contains("gpsLock")) {
            m_currentStatus.gpsLock = statusData["gpsLock"].toBool();
        }
        if (statusData.contains("gpsNumSats")) {
            m_currentStatus.gpsNumSats = statusData["gpsNumSats"].toInt();
        }
        if (statusData.contains("systemStatus")) {
            m_currentStatus.systemStatus = statusData["systemStatus"].toString();
        }
        if (statusData.contains("lastHeartbeat")) {
            m_currentStatus.lastHeartbeat = statusData["lastHeartbeat"].toString();
        }

        if (statusData.contains("position")) {
            QJsonObject pos = statusData["position"].toObject();
            m_currentStatus.position = QVector3D(
                pos["lat"].toDouble(),
                pos["lon"].toDouble(),
                pos["alt"].toDouble()
            );
            m_currentStatus.altitude = pos["alt"].toDouble();
        }

        if (statusData.contains("latitude") || statusData.contains("longitude")) {
            m_currentStatus.position.setX(static_cast<float>(statusData["latitude"].toDouble(m_currentStatus.position.x())));
            m_currentStatus.position.setY(static_cast<float>(statusData["longitude"].toDouble(m_currentStatus.position.y())));
        }
        if (statusData.contains("altitude")) {
            m_currentStatus.altitude = static_cast<float>(statusData["altitude"].toDouble());
            m_currentStatus.position.setZ(m_currentStatus.altitude);
        } else if (statusData.contains("gpsAltitude")) {
            m_currentStatus.position.setZ(static_cast<float>(statusData["gpsAltitude"].toDouble()));
        } else if (statusData.contains("localAltitude")) {
            m_currentStatus.altitude = static_cast<float>(statusData["localAltitude"].toDouble());
        }
        if (statusData.contains("groundSpeed")) {
            m_currentStatus.groundSpeed = static_cast<float>(statusData["groundSpeed"].toDouble());
        }
        if (statusData.contains("verticalSpeed")) {
            m_currentStatus.verticalSpeed = static_cast<float>(statusData["verticalSpeed"].toDouble());
        }
        if (statusData.contains("velocityX") || statusData.contains("velocityY") || statusData.contains("velocityZ")) {
            m_currentStatus.velocity = QVector3D(
                static_cast<float>(statusData["velocityX"].toDouble(m_currentStatus.velocity.x())),
                static_cast<float>(statusData["velocityY"].toDouble(m_currentStatus.velocity.y())),
                static_cast<float>(statusData["velocityZ"].toDouble(m_currentStatus.velocity.z()))
            );
        }
        if (statusData.contains("roll") || statusData.contains("pitch") || statusData.contains("yaw")) {
            m_currentStatus.attitude = QVector3D(
                static_cast<float>(statusData["roll"].toDouble(m_currentStatus.attitude.x())),
                static_cast<float>(statusData["pitch"].toDouble(m_currentStatus.attitude.y())),
                static_cast<float>(statusData["yaw"].toDouble(m_currentStatus.attitude.z()))
            );
        }

        emit statusUpdated(m_currentStatus);
    } else if (messageType == "error") {
        emit errorOccurred(data["message"].toString());
    } else if (messageType == "info") {
        emit messageReceived(data["message"].toString());
    }
}

void DroneController::onVOXLConnectionStatusChanged(bool connected)
{
    updateConnectionStatus(connected);
}

void DroneController::onVOXLError(const QString &error)
{
    emit errorOccurred(QString("VOXL Error: %1").arg(error));
}

void DroneController::uploadMission(const QVector<QVector3D> &waypoints)
{
    if (!m_connected) {
        emit errorOccurred("Cannot upload mission: Not connected to drone");
        return;
    }
    
    // Convert waypoints to mission items
    QVector<MissionItem> items = waypointsToMissionItems(waypoints);
    
    // Store current mission
    m_currentMission.id = QUuid::createUuid().toString();
    m_currentMission.name = QString("Mission_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    m_currentMission.items = items;
    m_currentMission.uploaded = false;
    
    // Create mission JSON
    QJsonArray waypointsArray;
    for (const MissionItem &item : items) {
        QJsonObject wpObj;
        wpObj["sequence"] = item.sequence;
        wpObj["command"] = item.command;
        wpObj["latitude"] = item.position.x();
        wpObj["longitude"] = item.position.y();
        wpObj["altitude"] = item.position.z();
        wpObj["param1"] = item.param1;
        wpObj["param2"] = item.param2;
        wpObj["param3"] = item.param3;
        wpObj["param4"] = item.param4;
        wpObj["autocontinue"] = item.autocontinue;
        waypointsArray.append(wpObj);
    }
    
    QJsonObject missionData;
    missionData["waypoints"] = waypointsArray;
    missionData["mission_id"] = m_currentMission.id;
    missionData["mission_name"] = m_currentMission.name;
    
    sendCommand("upload_mission", missionData);
    
    m_currentMission.uploaded = true;
    m_missionActive = false;
    m_missionPaused = false;
    emit missionStatusChanged("Mission uploaded successfully");
    emit messageReceived(QString("Uploaded mission with %1 waypoints").arg(items.size()));
}

void DroneController::uploadMapperMission(const std::vector<Waypoint> &waypoints)
{
    if (!m_connected) {
        emit errorOccurred("Cannot upload mapper mission: Not connected to drone");
        return;
    }
    if (!m_mapperClient || !m_mapperClient->isConnected()) {
        emit errorOccurred("Cannot upload mapper mission: VOXL Mapper portal sockets are not connected yet");
        return;
    }

    m_mapperMission = waypointsToMapperMission(waypoints);
    if (m_mapperMission.isEmpty()) {
        emit errorOccurred("Cannot upload mapper mission: no waypoints");
        return;
    }

    m_currentMission.id = QUuid::createUuid().toString();
    m_currentMission.name = QString("Mapper_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    m_currentMission.items.clear();
    for (const MapperMissionWaypoint &wp : m_mapperMission) {
        m_currentMission.items.append(MissionItem{wp.sequence,
                                                  QStringLiteral("MAPPER_PLAN_TO"),
                                                  wp.logicalPosition,
                                                  wp.holdTimeSec,
                                                  wp.acceptanceRadiusM,
                                                  wp.yawDeg,
                                                  0.0f,
                                                  true});
    }
    m_currentMission.uploaded = true;
    m_currentMissionItem = 0;
    m_resumeMissionItem = 0;
    m_missionActive = false;
    m_missionPaused = false;
    m_mapperMissionState = MapperMissionState::Idle;

    emit missionStatusChanged(QString("Mapper mission staged: %1 waypoint(s)").arg(m_mapperMission.size()));
    emit messageReceived("Mapper mission uses VOXL FRD commands: plan_to -> follow_path -> hold -> next.");
}

void DroneController::stageMissionLocally(const QVector<QVector3D> &waypoints)
{
    if (waypoints.isEmpty()) {
        emit errorOccurred("Cannot stage mission: no waypoints");
        return;
    }

    QVector<MissionItem> items = waypointsToMissionItems(waypoints);

    m_currentMission.id = QUuid::createUuid().toString();
    m_currentMission.name = QString("Local_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    m_currentMission.items = items;
    m_currentMission.uploaded = true;
    m_missionActive = false;
    m_missionPaused = false;

    emit missionStatusChanged("Mission staged locally (offline test)");
    emit messageReceived(QString("Offline test: staged %1 waypoints (not sent to drone)").arg(items.size()));
}

void DroneController::stageMapperMissionLocally(const std::vector<Waypoint> &waypoints)
{
    m_mapperMission = waypointsToMapperMission(waypoints);
    m_currentMission.id = QUuid::createUuid().toString();
    m_currentMission.name = QString("MapperLocal_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    m_currentMission.items.clear();
    for (const MapperMissionWaypoint &wp : m_mapperMission) {
        m_currentMission.items.append(MissionItem{wp.sequence,
                                                  QStringLiteral("MAPPER_PLAN_TO"),
                                                  wp.logicalPosition,
                                                  wp.holdTimeSec,
                                                  wp.acceptanceRadiusM,
                                                  wp.yawDeg,
                                                  0.0f,
                                                  true});
    }
    m_currentMission.uploaded = !m_mapperMission.isEmpty();
    m_missionActive = false;
    m_missionPaused = false;
    emit missionStatusChanged("Mapper mission staged locally (offline test)");
    emit messageReceived(QString("Offline test: staged %1 mapper waypoint(s)").arg(m_mapperMission.size()));
}

void DroneController::startMission()
{
    if (!m_connected) {
        emit errorOccurred("Cannot start mission: Not connected to drone");
        return;
    }
    
    if (!m_currentMission.uploaded) {
        emit errorOccurred("Cannot start mission: No mission uploaded");
        return;
    }
    
    if (m_mapperMission.isEmpty()) {
        emit errorOccurred("Cannot start mapper mission: no mapper waypoints staged");
        return;
    }
    if (!m_mapperClient || !m_mapperClient->isConnected()) {
        emit errorOccurred("Cannot start mapper mission: VOXL Mapper is not connected");
        return;
    }

    m_missionActive = true;
    m_missionPaused = false;
    m_currentMissionItem = 0;
    m_resumeMissionItem = 0;
    m_mapperMissionState = MapperMissionState::Planning;
    m_mapperStateTimer.restart();
    if (m_mapperMissionTimer)
        m_mapperMissionTimer->start();
    commandCurrentMapperWaypoint();

    emit missionStatusChanged("Mapper mission started");
    emit messageReceived("Mapper mission execution started");
}

void DroneController::pauseMission()
{
    if (!m_connected || !m_missionActive) {
        return;
    }
    
    if (m_mapperClient && m_mapperClient->isConnected())
        m_mapperClient->stopFollowing();
    m_resumeMissionItem = m_currentMissionItem;
    m_mapperMissionState = MapperMissionState::Paused;
    m_missionPaused = true;
    emit missionStatusChanged("Mapper mission paused");
}

void DroneController::resumeMission()
{
    if (!m_connected || !m_currentMission.uploaded) {
        return;
    }
    
    if (m_mapperMission.isEmpty()) {
        emit errorOccurred("Cannot resume mapper mission: no mapper waypoints staged");
        return;
    }
    if (!m_mapperClient || !m_mapperClient->isConnected()) {
        emit errorOccurred("Cannot resume mapper mission: VOXL Mapper is not connected");
        return;
    }

    m_currentMissionItem = qBound(0, m_resumeMissionItem, m_mapperMission.size() - 1);
    m_missionActive = true;
    m_missionPaused = false;
    m_mapperMissionState = MapperMissionState::Planning;
    m_mapperStateTimer.restart();
    if (m_mapperMissionTimer)
        m_mapperMissionTimer->start();
    commandCurrentMapperWaypoint();
    emit missionStatusChanged("Mapper mission resumed");
}

void DroneController::abortMission()
{
    if (!m_connected) {
        return;
    }

    releaseMapperTrajectoryToVehicle();
    emit missionStatusChanged("Mapper mission stopped");
    emit messageReceived("Mapper mission execution stopped");
}

void DroneController::clearMission()
{
    m_currentMission.items.clear();
    m_mapperMission.clear();
    m_currentMission.uploaded = false;
    m_missionActive = false;
    m_missionPaused = false;
    
    if (m_connected) {
        if (m_mapperClient && m_mapperClient->isConnected())
            m_mapperClient->stopFollowing();
    }
    
    emit missionStatusChanged("Mission cleared");
}

void DroneController::clearMapperMap()
{
    if (!m_mapperClient || !m_mapperClient->isMeshConnected()) {
        if (m_mapperClient && !m_droneHost.trimmed().isEmpty()) {
            m_pendingMapperMapCommand = PendingMapperMapCommand::Clear;
            m_mapperClient->connectToMapper(m_droneHost, 80);
            emit messageReceived("Connecting to VOXL Mapper mesh socket before clearing map...");
        } else {
            emit errorOccurred("Cannot clear map: VOXL Mapper is not connected");
        }
        return;
    }
    m_mapperClient->clearMap();
    emit messageReceived("VOXL Mapper clear map command sent");
}

void DroneController::loadMapperMap(const QString &remotePath)
{
    if (!m_mapperClient || !m_mapperClient->isMeshConnected()) {
        if (m_mapperClient && !m_droneHost.trimmed().isEmpty()) {
            m_pendingMapperMapCommand = PendingMapperMapCommand::Load;
            m_pendingMapperMapPath = remotePath;
            m_mapperClient->connectToMapper(m_droneHost, 80);
            emit messageReceived("Connecting to VOXL Mapper mesh socket before loading map...");
        } else {
            emit errorOccurred("Cannot load map: VOXL Mapper is not connected");
        }
        return;
    }
    m_mapperClient->loadMap(remotePath);
    emit messageReceived(remotePath.trimmed().isEmpty()
                             ? QStringLiteral("VOXL Mapper load default map command sent")
                             : QStringLiteral("VOXL Mapper load map command sent: %1").arg(remotePath.trimmed()));
}

void DroneController::restoreMapperMapFromBundle(const QString &localBundleDir, const QString &remoteMapPath)
{
    const QString remote = remoteMapPath.trimmed();
    const QString cleanLocal = QDir::toNativeSeparators(QDir::cleanPath(localBundleDir));
    if (cleanLocal.isEmpty() || !QDir(cleanLocal).exists()) {
        emit errorOccurred(QStringLiteral("Local map bundle folder does not exist."));
        return;
    }
    if (remote.isEmpty()) {
        emit errorOccurred(QStringLiteral("Remote map path in trajectory file is empty."));
        return;
    }
    if (!m_mapperClient || m_droneHost.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot restore bundled map: VOXL Mapper is not available."));
        return;
    }

    if (!m_mapperClient->isMeshConnected()) {
        m_pendingMapperMapCommand = PendingMapperMapCommand::RestoreFromBundle;
        m_pendingMapperMapPath = remote;
        m_pendingMapperBundleLocalDir = cleanLocal;
        m_mapperClient->connectToMapper(m_droneHost, 80);
        emit messageReceived(QStringLiteral("Connecting to VOXL Mapper mesh; will clear, upload bundled map, then load…"));
        return;
    }

    if (!m_voxlConnection) {
        emit errorOccurred(QStringLiteral("VOXL connection not available for scp."));
        return;
    }

    m_pendingBundleRestoreRemote = remote;
    m_mapperClient->clearMap();
    emit messageReceived(QStringLiteral("VOXL Mapper clear_map; uploading saved map bundle via scp…"));

    QTimer::singleShot(600, this, [this, cleanLocal, remote]() {
        if (!m_voxlConnection) {
            m_pendingBundleRestoreRemote.clear();
            return;
        }
        if (m_pendingBundleRestoreRemote.isEmpty() || m_pendingBundleRestoreRemote != remote)
            return;
        m_voxlConnection->uploadDirectoryToVoxl(cleanLocal, remote);
    });
}

void DroneController::replaceMapperMap(const QString &remotePath)
{
    const QString path = remotePath.trimmed();
    if (!m_mapperClient || m_droneHost.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot replace map: VOXL Mapper is not available."));
        return;
    }

    auto clearThenLoad = [this, path]() {
        if (!m_mapperClient || !m_mapperClient->isMeshConnected())
            return;
        m_mapperClient->clearMap();
        emit messageReceived(QStringLiteral("VOXL Mapper clear_map before load_map…"));
        QTimer::singleShot(600, this, [this, path]() {
            if (!m_mapperClient || !m_mapperClient->isMeshConnected())
                return;
            m_mapperClient->loadMap(path);
            emit messageReceived(path.isEmpty()
                                     ? QStringLiteral("VOXL Mapper load default map (after clear)")
                                     : QStringLiteral("VOXL Mapper load map (after clear): %1").arg(path));
        });
    };

    if (!m_mapperClient->isMeshConnected()) {
        m_pendingMapperMapCommand = PendingMapperMapCommand::LoadAfterClear;
        m_pendingMapperMapPath = path;
        m_mapperClient->connectToMapper(m_droneHost, 80);
        emit messageReceived(QStringLiteral("Connecting to VOXL Mapper mesh socket; will clear then load map…"));
        return;
    }

    clearThenLoad();
}

void DroneController::saveMapperMap(const QString &format, const QString &remotePath)
{
    if (!m_mapperClient || !m_mapperClient->isMeshConnected()) {
        if (m_mapperClient && !m_droneHost.trimmed().isEmpty()) {
            m_pendingMapperMapCommand = PendingMapperMapCommand::Save;
            m_pendingMapperMapPath = remotePath;
            m_pendingMapperMapFormat = format;
            m_mapperClient->connectToMapper(m_droneHost, 80);
            emit messageReceived("Connecting to VOXL Mapper mesh socket before saving map...");
        } else {
            emit errorOccurred("Cannot save map: VOXL Mapper is not connected");
        }
        return;
    }
    m_mapperClient->saveMap(format, remotePath);
    emit messageReceived(remotePath.trimmed().isEmpty()
                             ? QStringLiteral("VOXL Mapper save map command sent")
                             : QStringLiteral("VOXL Mapper save map command sent: %1").arg(remotePath.trimmed()));
}

void DroneController::downloadMapperMapFromVehicle(const QString &localDir, const QString &remotePath)
{
    if (!m_voxlConnection) {
        emit mapperBundleDownloadFinished(false, QStringLiteral("VOXL connection not available."));
        return;
    }
    m_voxlConnection->downloadDirectoryFromVoxl(localDir, remotePath);
}

bool DroneController::isMapperMeshConnected() const
{
    return m_mapperClient && m_mapperClient->isMeshConnected();
}

void DroneController::uploadMapperMap(const QString &localFilePath, const QString &remotePath)
{
    if (!m_connected || !m_voxlConnection) {
        emit errorOccurred("Cannot upload map: Not connected to VOXL");
        return;
    }

    const QString trimmedRemotePath = remotePath.trimmed();
    if (trimmedRemotePath.isEmpty()) {
        emit errorOccurred("Cannot upload map: Remote VOXL path is empty");
        return;
    }

    m_voxlConnection->uploadFileToVoxl(localFilePath, trimmedRemotePath, QStringLiteral("mapper map"));
    emit messageReceived(QStringLiteral("Uploading mapper map to VOXL: %1").arg(trimmedRemotePath));
}

void DroneController::onMapperMeshConnectedChanged(bool connected)
{
    if (connected)
        stopMapperPortalWatchdog();

    if (!connected || m_pendingMapperMapCommand == PendingMapperMapCommand::None)
        return;

    const PendingMapperMapCommand command = m_pendingMapperMapCommand;
    const QString path = m_pendingMapperMapPath;
    const QString format = m_pendingMapperMapFormat;
    const QString bundleDir = m_pendingMapperBundleLocalDir;
    m_pendingMapperMapCommand = PendingMapperMapCommand::None;
    m_pendingMapperMapPath.clear();
    m_pendingMapperMapFormat.clear();
    m_pendingMapperBundleLocalDir.clear();

    switch (command) {
    case PendingMapperMapCommand::Load:
        loadMapperMap(path);
        break;
    case PendingMapperMapCommand::Save:
        saveMapperMap(format.isEmpty() ? QStringLiteral("ply") : format, path);
        break;
    case PendingMapperMapCommand::Clear:
        clearMapperMap();
        break;
    case PendingMapperMapCommand::LoadAfterClear:
        replaceMapperMap(path);
        break;
    case PendingMapperMapCommand::RestoreFromBundle:
        restoreMapperMapFromBundle(bundleDir, path);
        break;
    case PendingMapperMapCommand::None:
        break;
    }
}

void DroneController::onMapperPortalWatchdogTimeout()
{
    if (m_mapperPortalWatchdog) {
        m_mapperPortalWatchdog->deleteLater();
        m_mapperPortalWatchdog = nullptr;
    }
    if (m_silMode || !m_connected)
        return;
    if (m_mapperClient && !m_mapperClient->isMeshConnected()) {
        emit messageReceived(QStringLiteral(
            "VOXL Mapper portal not detected on port 80 (mesh socket). "
            "Start voxl-portal on the drone to stream the live map."));
    }
}

void DroneController::onMapperBundleUploadForRestoreFinished(bool success, const QString &msg)
{
    if (m_pendingBundleRestoreRemote.isEmpty())
        return;
    const QString remote = m_pendingBundleRestoreRemote;
    m_pendingBundleRestoreRemote.clear();

    if (!success) {
        emit errorOccurred(QStringLiteral("Bundled map upload failed: %1").arg(msg));
        return;
    }
    if (!m_mapperClient || !m_mapperClient->isMeshConnected()) {
        emit errorOccurred(QStringLiteral("Mapper disconnected before load_map could run."));
        return;
    }
    m_mapperClient->loadMap(remote);
    emit messageReceived(QStringLiteral("VOXL Mapper load_map after bundle upload: %1").arg(remote));
    emit messageReceived(QStringLiteral("Live mesh stream continues from the mapper service."));
}

void DroneController::stopMapperPortalWatchdog()
{
    if (!m_mapperPortalWatchdog)
        return;
    m_mapperPortalWatchdog->stop();
    m_mapperPortalWatchdog->deleteLater();
    m_mapperPortalWatchdog = nullptr;
}

void DroneController::planMapperHome()
{
    if (!m_mapperClient || !m_mapperClient->isConnected()) {
        emit errorOccurred("Cannot plan home: VOXL Mapper is not connected");
        return;
    }
    m_mapperClient->planHome();
    emit messageReceived("VOXL Mapper plan_home command sent");
}

void DroneController::setManualControl(float roll, float pitch, float yaw, float throttle)
{
    if (!m_connected) {
        emit errorOccurred("Cannot send manual control: Not connected to drone");
        return;
    }

    QJsonObject params;
    params["roll"] = roll;
    params["pitch"] = pitch;
    params["yaw"] = yaw;
    params["throttle"] = throttle;
    sendCommand("set_manual_control", params);
}

void DroneController::setPositionTarget(const QVector3D &position)
{
    if (!m_connected) {
        emit errorOccurred("Cannot set position target: Not connected to drone");
        return;
    }

    QJsonObject params;
    params["x"] = position.x();
    params["y"] = position.y();
    params["z"] = position.z();
    sendCommand("set_position_target", params);
}

void DroneController::setVelocityTarget(const QVector3D &velocity)
{
    if (!m_connected) {
        emit errorOccurred("Cannot set velocity target: Not connected to drone");
        return;
    }

    QJsonObject params;
    params["x"] = velocity.x();
    params["y"] = velocity.y();
    params["z"] = velocity.z();
    sendCommand("set_velocity_target", params);
}

void DroneController::startVideoRecording()
{
    sendCommand("start_video_recording");
}

void DroneController::stopVideoRecording()
{
    sendCommand("stop_video_recording");
}

void DroneController::takePicture()
{
    sendCommand("take_picture");
}

void DroneController::setCameraSettings(const QString &mode, int quality)
{
    sendCommand("set_camera_settings", QJsonObject{{"mode", mode}, {"quality", quality}});
}

void DroneController::requestStatus()
{
    if (m_voxlConnection && m_connected) {
        m_voxlConnection->requestStatus();
    }
}

void DroneController::processStatusData(const QJsonObject &data)
{
    Q_UNUSED(data);
}

void DroneController::processMissionStatus(const QJsonObject &data)
{
    Q_UNUSED(data);
}

void DroneController::sendMavlinkCommand(int command, float param1, float param2, float param3, float param4, float param5, float param6, float param7)
{
    QJsonObject params;
    params["command"] = command;
    params["param1"] = param1;
    params["param2"] = param2;
    params["param3"] = param3;
    params["param4"] = param4;
    params["param5"] = param5;
    params["param6"] = param6;
    params["param7"] = param7;
    sendCommand("mavlink_command_long", params);
}

QVector<MissionItem> DroneController::waypointsToMissionItems(const QVector<QVector3D> &waypoints)
{
    QVector<MissionItem> items;
    
    for (int i = 0; i < waypoints.size(); ++i) {
        MissionItem item;
        item.sequence = i;
        item.command = "NAV_WAYPOINT";
        item.position = waypoints[i];
        item.param1 = 0.0f;  // Hold time
        item.param2 = 2.0f;  // Acceptance radius
        item.param3 = 0.0f;  // Pass through
        item.param4 = qDegreesToRadians(0.0f);  // Yaw angle
        item.autocontinue = true;
        items.append(item);
    }
    
    return items;
}

QVector<MapperMissionWaypoint> DroneController::waypointsToMapperMission(const std::vector<Waypoint> &waypointsIn) const
{
    std::vector<Waypoint> waypoints = waypointsIn;
    waypoints.erase(std::remove_if(waypoints.begin(), waypoints.end(),
                                   [](const Waypoint &w) { return w.isMapperHome(); }),
                    waypoints.end());

    QVector<MapperMissionWaypoint> mission;
    mission.reserve(static_cast<int>(waypoints.size()));
    for (const Waypoint &wp : waypoints) {
        MapperMissionWaypoint item;
        item.sequence = wp.sequence();
        item.logicalPosition = QVector3D(wp.x(), wp.y(), wp.z());
        item.frdPosition = logicalToMapperFrd(item.logicalPosition);
        item.holdTimeSec = qMax(0.0f, wp.holdTime());
        item.acceptanceRadiusM = qMax(0.1f, wp.acceptanceRadius());
        item.yawDeg = wp.yawAngle();
        mission.append(item);
    }
    return mission;
}

QVector3D DroneController::logicalToMapperFrd(const QVector3D &logicalPosition)
{
    return QVector3D(logicalPosition.x(), -logicalPosition.y(), -logicalPosition.z());
}

QVector3D DroneController::mapperFrdToLogical(const QVector3D &positionFrd)
{
    return QVector3D(positionFrd.x(), -positionFrd.y(), -positionFrd.z());
}

void DroneController::onMapperPoseReceived(const QVector3D &positionFrd, const QVector3D &velocityFrd, float yawRad)
{
    m_mapperPositionFrd = positionFrd;
    m_mapperVelocityFrd = velocityFrd;
    m_haveMapperPose = true;

    // Keep the UI in the planner's logical frame while retaining FRD internally.
    m_currentStatus.position = mapperFrdToLogical(positionFrd);
    m_currentStatus.velocity = mapperFrdToLogical(velocityFrd);
    m_currentStatus.altitude = m_currentStatus.position.z();
    m_currentStatus.attitude.setZ(-qRadiansToDegrees(yawRad));
    emit mapperPoseUpdated(m_currentStatus.position, m_currentStatus.attitude.z());
    emit statusUpdated(m_currentStatus);
}

void DroneController::onMapperRenderReceived(const QString &name, int format, const QVector<MapperPathPoint> &points)
{
    Q_UNUSED(format);

    QVector<QVector3D> positionsLogical;
    QVector<QColor> colors;
    positionsLogical.reserve(points.size());
    colors.reserve(points.size());

    for (const MapperPathPoint &point : points) {
        positionsLogical.append(mapperFrdToLogical(point.positionFrd));
        colors.append(point.color.isValid() ? point.color : QColor(110, 160, 210));
    }

    emit mapperRenderUpdated(positionsLogical, colors);
    if (!points.isEmpty())
        emit messageReceived(QStringLiteral("VOXL Mapper render update: %1 points from %2")
                                 .arg(points.size())
                                 .arg(name.trimmed().isEmpty() ? QStringLiteral("mesh/plan") : name.trimmed()));
}

void DroneController::onMapperMeshReceived(const QVector<MapperPathPoint> &vertices, const QVector<quint32> &triangleIndices)
{
    QVector<QVector3D> positionsLogical;
    QVector<QColor> colors;
    positionsLogical.reserve(vertices.size());
    colors.reserve(vertices.size());

    for (const MapperPathPoint &vertex : vertices) {
        positionsLogical.append(mapperFrdToLogical(vertex.positionFrd));
        colors.append(vertex.color.isValid() ? vertex.color : QColor(110, 160, 210));
    }

    emit mapperMeshUpdated(positionsLogical, colors, triangleIndices);
    if (vertices.isEmpty())
        emit messageReceived(QStringLiteral("VOXL Mapper mesh cleared (0 vertices)"));
    else
        emit messageReceived(QStringLiteral("VOXL Mapper mesh update: %1 vertices, %2 triangles")
                                 .arg(vertices.size())
                                 .arg(triangleIndices.size() / 3));
}

void DroneController::onMapperTick()
{
    if (!m_missionActive || m_missionPaused || m_mapperMissionState == MapperMissionState::Idle)
        return;

    if (m_currentMissionItem < 0 || m_currentMissionItem >= m_mapperMission.size()) {
        finishMapperMission(QStringLiteral("Mapper mission complete"));
        return;
    }

    const MapperMissionWaypoint &target = m_mapperMission[m_currentMissionItem];

    if (m_mapperMissionState == MapperMissionState::Planning) {
        if (m_mapperStateTimer.elapsed() > 1000) {
            if (m_mapperClient && m_mapperClient->isConnected())
                m_mapperClient->followPath();
            m_mapperMissionState = MapperMissionState::Following;
            m_mapperStateTimer.restart();
            emit missionStatusChanged(QString("Following mapper waypoint %1/%2")
                                          .arg(m_currentMissionItem + 1)
                                          .arg(m_mapperMission.size()));
        }
        return;
    }

    if (m_mapperMissionState == MapperMissionState::Following) {
        if (!m_haveMapperPose) {
            if (m_mapperStateTimer.elapsed() > 8000)
                emit warningIssued("Waiting for VOXL Mapper pose before waypoint arrival checks.");
            return;
        }

        const float distance = (m_mapperPositionFrd - target.frdPosition).length();
        const float speed = m_mapperVelocityFrd.length();
        if (distance <= target.acceptanceRadiusM && speed < 0.35f) {
            if (target.holdTimeSec > 0.0f) {
                m_mapperMissionState = MapperMissionState::Holding;
                m_mapperHoldTimer.restart();
                emit missionStatusChanged(QString("Holding waypoint %1 for %2 s")
                                              .arg(m_currentMissionItem + 1)
                                              .arg(target.holdTimeSec, 0, 'f', 1));
            } else {
                advanceMapperMission();
            }
        } else if (m_mapperStateTimer.elapsed() > 120000) {
            emit warningIssued(QString("Mapper waypoint %1 has been active for over 120 seconds.").arg(m_currentMissionItem + 1));
            m_mapperStateTimer.restart();
        }
        return;
    }

    if (m_mapperMissionState == MapperMissionState::Holding) {
        if (m_mapperHoldTimer.elapsed() >= static_cast<qint64>(target.holdTimeSec * 1000.0f)) {
            advanceMapperMission();
        }
    }
}

void DroneController::advanceMapperMission()
{
    ++m_currentMissionItem;
    if (m_currentMissionItem >= m_mapperMission.size()) {
        finishMapperMission(QStringLiteral("Mapper mission complete"));
        return;
    }
    commandCurrentMapperWaypoint();
}

void DroneController::commandCurrentMapperWaypoint()
{
    if (!m_mapperClient || !m_mapperClient->isConnected())
        return;
    if (m_currentMissionItem < 0 || m_currentMissionItem >= m_mapperMission.size())
        return;

    const MapperMissionWaypoint &target = m_mapperMission[m_currentMissionItem];
    m_mapperClient->planToFrd(target.frdPosition);
    m_mapperMissionState = MapperMissionState::Planning;
    m_mapperStateTimer.restart();
    emit missionStatusChanged(QString("Planning mapper waypoint %1/%2: FRD (%3, %4, %5)")
                                  .arg(m_currentMissionItem + 1)
                                  .arg(m_mapperMission.size())
                                  .arg(target.frdPosition.x(), 0, 'f', 2)
                                  .arg(target.frdPosition.y(), 0, 'f', 2)
                                  .arg(target.frdPosition.z(), 0, 'f', 2));
}

void DroneController::finishMapperMission(const QString &message)
{
    if (m_mapperMissionTimer)
        m_mapperMissionTimer->stop();
    if (m_mapperClient && m_mapperClient->isConnected())
        m_mapperClient->stopFollowing();
    m_mapperMissionState = MapperMissionState::Idle;
    m_missionActive = false;
    m_missionPaused = false;
    m_resumeMissionItem = 0;
    emit missionStatusChanged(message);
    emit messageReceived(message);
}