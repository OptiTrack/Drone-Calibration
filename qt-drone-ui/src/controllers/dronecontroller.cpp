#include "dronecontroller.h"
#include "../network/voxlconnection.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QUuid>
#include <QVector3D>
#include <QtMath>

DroneController::DroneController(QObject *parent)
    : QObject(parent)
    , m_voxlConnection(nullptr)
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
    , m_manualControlActive(false)
    , m_manualControlTimer(nullptr)
    , m_recordingFlight(false)
    , m_lastRecordedPointMs(0)
    , m_playbackActive(false)
    , m_playbackIndex(0)
    , m_playbackTickTimer(nullptr)
    , m_runTrajectoryAfterUpload(false)
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
    m_currentStatus.slamPosition = QVector3D(0, 0, 0);
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
    
    // Connect signals
    connect(m_voxlConnection, &VOXLConnection::connected,
            this, [this]() { onVOXLConnectionStatusChanged(true); });
    connect(m_voxlConnection, &VOXLConnection::disconnected,
            this, [this]() { onVOXLConnectionStatusChanged(false); });
    connect(m_voxlConnection, &VOXLConnection::dataReceived,
            this, &DroneController::onVOXLDataReceived);
    connect(m_voxlConnection, &VOXLConnection::errorOccurred,
            this, &DroneController::onVOXLError);
    connect(m_voxlConnection, &VOXLConnection::missionUploadComplete,
            this, [this]() {
                if (!m_runTrajectoryAfterUpload) {
                    return;
                }
                m_runTrajectoryAfterUpload = false;
                const QString fileName = m_pendingTrajectoryFileName.isEmpty()
                    ? QStringLiteral("trajectory.json")
                    : m_pendingTrajectoryFileName;
                m_voxlConnection->runMission(fileName);
                emit messageReceived(QString("Started uploaded trajectory: %1").arg(fileName));
            });
    
    // Set up timers
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(1000); // 1 Hz heartbeat
    connect(m_heartbeatTimer, &QTimer::timeout, this, &DroneController::onHeartbeatTimer);
    
    m_statusUpdateTimer = new QTimer(this);
    m_statusUpdateTimer->setInterval(100); // 10 Hz status updates
    connect(m_statusUpdateTimer, &QTimer::timeout, this, &DroneController::onStatusUpdateTimer);
    
    // Manual control timer
    m_manualControlTimer = new QTimer(this);
    m_manualControlTimer->setInterval(50); // 20 Hz manual control

    // Trajectory playback timer
    m_playbackTickTimer = new QTimer(this);
    m_playbackTickTimer->setInterval(20); // 50 Hz scheduler for smooth timing
    connect(m_playbackTickTimer, &QTimer::timeout,
            this, &DroneController::onTrajectoryPlaybackTick);
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
    } else {
        emit errorOccurred("Failed to initiate connection");
    }
    
    return success;
}

void DroneController::disconnectFromDrone()
{
    if (m_voxlConnection) {
        m_voxlConnection->disconnect();
    }
    
    updateConnectionStatus(false);
    emit messageReceived("Disconnected from drone");
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
            m_currentStatus.slamPosition = QVector3D(0, 0, 0);
            m_currentStatus.lastHeartbeat.clear();
            m_currentStatus.systemStatus = "DISCONNECTED";
            m_missionActive = false;
            m_missionPaused = false;
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

    sendCommand("land");
    emit messageReceived("Land command sent");
}

void DroneController::returnToLaunch()
{
    if (!m_connected) {
        emit errorOccurred("Cannot return to launch: Not connected to drone");
        return;
    }

    sendCommand("return_to_launch");
    emit messageReceived("Return-to-launch command sent");
}

void DroneController::emergencyStop()
{
    if (!m_connected) {
        emit errorOccurred("Cannot emergency stop: Not connected to drone");
        return;
    }

    sendCommand("emergency_stop");
    m_missionActive = false;
    m_missionPaused = false;
    emit missionStatusChanged("Emergency stop requested");
    emit messageReceived("Emergency stop command sent");
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
        // VIO/SLAM local NED position (from LOCAL_POSITION_NED, msg 32)
        if (statusData.contains("localX") || statusData.contains("localY")) {
            m_currentStatus.slamPosition.setX(
                static_cast<float>(statusData["localX"].toDouble(m_currentStatus.slamPosition.x())));
            m_currentStatus.slamPosition.setY(
                static_cast<float>(statusData["localY"].toDouble(m_currentStatus.slamPosition.y())));
        }
        if (statusData.contains("localAltitude")) {
            m_currentStatus.slamPosition.setZ(
                static_cast<float>(statusData["localAltitude"].toDouble(m_currentStatus.slamPosition.z())));
        }

        if (m_recordingFlight) {
            const qint64 nowMs = m_recordingTimer.elapsed();
            constexpr qint64 kMinSpacingMs = 200; // ~5 Hz sample rate for path recording
            if (nowMs - m_lastRecordedPointMs >= kMinSpacingMs) {
                Waypoint wp(m_currentStatus.slamPosition,
                            QString("REC %1").arg(m_recordingPath.waypointCount() + 1));
                wp.setTimestampMs(nowMs);
                wp.setYawAngle(m_currentStatus.attitude.z());
                wp.setSpeed(qMax(0.0f, m_currentStatus.groundSpeed));
                m_recordingPath.addWaypoint(wp);
                m_lastRecordedPointMs = nowMs;
            }
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
    
    sendCommand("start_mission");
    m_missionActive = true;
    m_missionPaused = false;
    m_currentMissionItem = 0;
    
    emit missionStatusChanged("Mission started");
    emit messageReceived("Mission execution started");
}

void DroneController::pauseMission()
{
    if (!m_connected || !m_missionActive) {
        return;
    }
    
    sendCommand("pause_mission");
    m_missionPaused = true;
    emit missionStatusChanged("Mission paused");
}

void DroneController::resumeMission()
{
    if (!m_connected || !m_currentMission.uploaded) {
        return;
    }
    
    sendCommand("resume_mission");
    m_missionActive = true;
    m_missionPaused = false;
    emit missionStatusChanged("Mission resumed");
}

void DroneController::abortMission()
{
    if (!m_connected) {
        return;
    }
    
    sendCommand("abort_mission");
    m_missionActive = false;
    m_missionPaused = false;
    
    emit missionStatusChanged("Mission aborted");
    emit messageReceived("Mission execution aborted");
}

void DroneController::clearMission()
{
    m_currentMission.items.clear();
    m_currentMission.uploaded = false;
    m_missionActive = false;
    m_missionPaused = false;
    
    if (m_connected) {
        sendCommand("clear_mission");
    }
    
    emit missionStatusChanged("Mission cleared");
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

void DroneController::startFlightRecording(const QString &name)
{
    if (m_recordingFlight) {
        emit warningIssued("Flight recording already in progress");
        return;
    }

    const QString recordingName = name.trimmed().isEmpty()
        ? QString("ManualFlight_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"))
        : name.trimmed();

    m_recordingPath = FlightPath(recordingName);
    m_recordingPath.setDescription("Recorded manual flight trajectory");
    m_recordingTimer.restart();
    m_lastRecordedPointMs = 0;
    m_recordingFlight = true;

    emit messageReceived(QString("Flight recording started: %1").arg(recordingName));
    emit flightRecordingStarted(recordingName);
}

FlightPath DroneController::stopFlightRecording()
{
    if (!m_recordingFlight) {
        return FlightPath();
    }

    m_recordingFlight = false;
    m_recordingPath.updateSequences();

    emit messageReceived(QString("Flight recording stopped: %1 points")
                         .arg(m_recordingPath.waypointCount()));
    emit flightRecordingStopped(m_recordingPath.name(), m_recordingPath.waypointCount());
    return m_recordingPath;
}

bool DroneController::saveTrajectoryToFile(const FlightPath &path, const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        emit errorOccurred(QString("Failed to write trajectory file: %1").arg(file.errorString()));
        return false;
    }

    QJsonDocument doc(path.toRunnerTrajectoryJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool DroneController::loadTrajectoryFromFile(const QString &filePath, FlightPath *outPath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(QString("Failed to open trajectory file: %1").arg(file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit errorOccurred(QString("Invalid trajectory JSON: %1").arg(parseError.errorString()));
        return false;
    }

    FlightPath parsed = FlightPath::fromRunnerTrajectoryJson(doc.object());
    if (parsed.waypointCount() <= 0) {
        emit errorOccurred("Trajectory file contains no waypoints");
        return false;
    }

    if (outPath) {
        *outPath = parsed;
    }
    return true;
}

void DroneController::uploadAndPlayTrajectory(const FlightPath &path, const QString &remotePath)
{
    if (!m_voxlConnection || !m_connected) {
        emit errorOccurred("Cannot upload trajectory: not connected");
        return;
    }
    if (path.waypointCount() <= 0) {
        emit errorOccurred("Cannot upload trajectory: no waypoints");
        return;
    }

    const QString tempPath = QString("/tmp/%1_trajectory.json")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!saveTrajectoryToFile(path, tempPath)) {
        return;
    }

    m_pendingTrajectoryFileName = QFileInfo(remotePath).fileName();
    if (m_pendingTrajectoryFileName.isEmpty()) {
        m_pendingTrajectoryFileName = QStringLiteral("trajectory.json");
    }
    m_runTrajectoryAfterUpload = true;
    m_voxlConnection->uploadMissionFile(tempPath, remotePath);
    emit messageReceived(QString("Uploading trajectory for autonomous run: %1").arg(path.name()));
}

void DroneController::startTrajectoryPlayback(const FlightPath &trajectory)
{
    if (!m_connected) {
        emit errorOccurred("Cannot playback trajectory: not connected");
        return;
    }
    if (trajectory.waypointCount() <= 0) {
        emit errorOccurred("Cannot playback trajectory: no waypoints");
        return;
    }

    m_playbackPath = trajectory;
    m_playbackIndex = 0;
    m_playbackActive = true;
    m_playbackTimer.restart();

    if (m_playbackTickTimer) {
        m_playbackTickTimer->start();
    }

    emit messageReceived(QString("Trajectory playback started: %1 (%2 points)")
                         .arg(trajectory.name())
                         .arg(trajectory.waypointCount()));
    emit trajectoryPlaybackStarted(trajectory.name(), trajectory.waypointCount());
}

void DroneController::stopTrajectoryPlayback()
{
    if (!m_playbackActive) {
        return;
    }

    m_playbackActive = false;
    if (m_playbackTickTimer) {
        m_playbackTickTimer->stop();
    }

    emit messageReceived(QString("Trajectory playback finished: %1").arg(m_playbackPath.name()));
    emit trajectoryPlaybackFinished(m_playbackPath.name());
}

void DroneController::onTrajectoryPlaybackTick()
{
    if (!m_playbackActive || !m_connected) {
        stopTrajectoryPlayback();
        return;
    }
    if (m_playbackIndex >= m_playbackPath.waypointCount()) {
        stopTrajectoryPlayback();
        return;
    }

    const qint64 nowMs = m_playbackTimer.elapsed();
    const Waypoint &nextWp = m_playbackPath.waypoint(m_playbackIndex);
    const qint64 targetMs = nextWp.timestampMs();

    // If timestamps are not provided, fall back to immediate sequential playback.
    const bool due = (targetMs <= 0) || (nowMs >= targetMs);
    if (!due) {
        return;
    }

    setPositionTarget(nextWp.position());
    emit trajectoryPlaybackProgress(m_playbackIndex + 1, m_playbackPath.waypointCount());
    ++m_playbackIndex;

    if (m_playbackIndex >= m_playbackPath.waypointCount()) {
        stopTrajectoryPlayback();
    }
}