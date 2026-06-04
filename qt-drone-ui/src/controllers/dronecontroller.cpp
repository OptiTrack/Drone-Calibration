#include "dronecontroller.h"
#include "../network/voxlconnection.h"
#include "../utils/voxlmappaths.h"
#include "../network/voxlmapperclient.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>
#include <QUuid>
#include <QVector3D>
#include <QtMath>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <algorithm>
#include <limits>

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
    , m_mapperPlanReceivedForCurrentTarget(false)
    , m_mapperFollowIssuedForCurrentTarget(false)
    , m_mapperPlanMismatchWarnedForCurrentTarget(false)
    , m_resumeMissionItem(0)
    , m_pendingMapperMapCommand(PendingMapperMapCommand::None)
    , m_mapperPortalWatchdog(nullptr)
    , m_thermalPollTimer(nullptr)
    , m_paramPollTimer(nullptr)
    , m_navDllActKnown(false)
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
    m_currentStatus.positionIsMapperLocal = false;
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
    connect(m_voxlConnection, &VOXLConnection::px4ParameterReceived,
            this, [this](const QString &paramId, float value) {
                if (paramId == QStringLiteral("NAV_DLL_ACT") && !m_navDllActKnown) {
                    m_navDllActKnown = true;
                    if (m_paramPollTimer)
                        m_paramPollTimer->stop();
                }
                emit px4ParameterReceived(paramId, value);
            });

    // Poll for NAV_DLL_ACT every 2 s until it is received (PX4 often misses the first request).
    m_paramPollTimer = new QTimer(this);
    m_paramPollTimer->setInterval(2000);
    connect(m_paramPollTimer, &QTimer::timeout, this, [this]() {
        if (m_connected && !m_navDllActKnown)
            requestPx4Parameter(QStringLiteral("NAV_DLL_ACT"));
    });
    connect(m_voxlConnection, &VOXLConnection::sshCommandFinished,
            this, &DroneController::onSshCommandFinished);
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
            this, [this](const QString &status) {
                emit messageReceived(status);
                // When the pose socket drops, clear the pose flag immediately so the
                // mission state machine stops computing distances from stale data.
                if (status.contains(QLatin1String("pose socket disconnected"), Qt::CaseInsensitive))
                    m_haveMapperPose = false;
            });
    
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

    // Thermal/CPU poll: every 10 seconds via SSH (only fires when connected)
    m_thermalPollTimer = new QTimer(this);
    m_thermalPollTimer->setInterval(10000);
    connect(m_thermalPollTimer, &QTimer::timeout, this, &DroneController::onThermalPollTimer);
}

bool DroneController::connectToDrone(const QString &host, int port, const QString &sshPassword)
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
    m_voxlConnection->setVoxlSshPassword(sshPassword);

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
    m_sshKindQueue.clear();
    m_sshPollSubdirQueue.clear();
    if (m_voxlConnection) {
        m_voxlConnection->disconnect();
    }
    if (m_mapperClient && m_mapperClient->isPlanConnected()) {
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
    if (m_mapperClient && m_mapperClient->isPlanConnected())
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
            m_thermalPollTimer->start();
            // Read current arming mode; retry every 2 s until PX4 responds.
            m_navDllActKnown = false;
            requestPx4Parameter(QStringLiteral("NAV_DLL_ACT"));
            m_paramPollTimer->start();
        } else {
            m_sshKindQueue.clear();
            m_sshPollSubdirQueue.clear();
            m_navDllActKnown = false;
            m_paramPollTimer->stop();
            m_heartbeatTimer->stop();
            m_statusUpdateTimer->stop();
            m_thermalPollTimer->stop();
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
            m_currentStatus.positionIsMapperLocal = false;
            m_currentStatus.velocity = QVector3D(0, 0, 0);
            m_currentStatus.attitude = QVector3D(0, 0, 0);
            m_currentStatus.lastHeartbeat.clear();
            m_currentStatus.systemStatus = "DISCONNECTED";
            m_currentStatus.px4LoadPercent = -1.0f;
            m_currentStatus.voxlTempC = -1.0f;
            m_currentStatus.voxlTopService.clear();
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

void DroneController::requestPx4Parameter(const QString &paramId)
{
    if (!m_connected) {
        return;
    }
    QJsonObject params;
    params["param_id"] = paramId;
    sendCommand("request_param", params);
}

void DroneController::setPx4Parameter(const QString &paramId, float value, quint8 paramType)
{
    if (!m_connected) {
        emit errorOccurred("Cannot set parameter: not connected to drone");
        return;
    }
    QJsonObject params;
    params["param_id"]   = paramId;
    params["value"]      = static_cast<double>(value);
    params["param_type"] = static_cast<int>(paramType);
    sendCommand("set_param", params);
    emit messageReceived(QString("Parameter %1 set to %2").arg(paramId).arg(value));
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
        if (statusData.contains("px4LoadPercent")) {
            m_currentStatus.px4LoadPercent = static_cast<float>(statusData["px4LoadPercent"].toDouble());
        }
        if (statusData.contains("flightMode")) {
            const QString newMode = statusData["flightMode"].toString();
            const QString prevMode = m_prevFlightMode;
            m_currentStatus.flightMode = newMode;

            // Safety check: if an autonomous mapper mission is running and PX4
            // unexpectedly switches to a manual-family mode (indicating a VIO
            // failure or a firmware failsafe), stop the trajectory sequencer and
            // command a gentle land so the drone does not drift uncontrolled.
            if (!newMode.isEmpty() && newMode != prevMode) {
                const QString newModeLower = newMode.toLower();
                const bool isManualFamily = (newModeLower == QLatin1String("manual")
                                             || newModeLower == QLatin1String("stabilized")
                                             || newModeLower == QLatin1String("acro")
                                             || newModeLower == QLatin1String("rattitude"));
                if (isManualFamily && (m_missionActive || m_mapperMissionState != MapperMissionState::Idle)) {
                    emit warningIssued(QStringLiteral(
                        "SAFETY: Flight mode changed from \"%1\" to \"%2\" during active mission. "
                        "Stopping trajectory and commanding land.")
                            .arg(prevMode.isEmpty() ? QStringLiteral("(unknown)") : prevMode)
                            .arg(newMode));
                    releaseMapperTrajectoryToVehicle();
                    // Only send the land command if we still have a connection.
                    if (m_connected)
                        sendCommand(QStringLiteral("land"));
                    emit missionStatusChanged(QStringLiteral(
                        "Mission aborted \u2014 unexpected mode switch to %1. Land command sent.").arg(newMode));
                }
            }
            m_prevFlightMode = newMode;
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
            m_currentStatus.positionIsMapperLocal = false;
            m_currentStatus.altitude = pos["alt"].toDouble();
        }

        if (statusData.contains("latitude") || statusData.contains("longitude")) {
            m_currentStatus.positionIsMapperLocal = false;
            m_currentStatus.position.setX(static_cast<float>(statusData["latitude"].toDouble(m_currentStatus.position.x())));
            m_currentStatus.position.setY(static_cast<float>(statusData["longitude"].toDouble(m_currentStatus.position.y())));
        }
        if (statusData.contains("altitude")) {
            m_currentStatus.altitude = static_cast<float>(statusData["altitude"].toDouble());
            if (!m_currentStatus.positionIsMapperLocal)
                m_currentStatus.position.setZ(m_currentStatus.altitude);
        } else if (statusData.contains("gpsAltitude")) {
            if (!m_currentStatus.positionIsMapperLocal)
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
    // Only the /plan socket is required for mission execution.
    // The /mesh socket is only needed for map ops (clear/load/save).
    if (!m_mapperClient || !m_mapperClient->isPlanConnected()) {
        emit errorOccurred("Cannot upload mapper mission: VOXL Mapper plan socket is not connected");
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
    logMapperMissionSummary(QStringLiteral("Uploaded mapper mission"));
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
    logMapperMissionSummary(QStringLiteral("Offline mapper mission"));
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
    if (!m_mapperClient || !m_mapperClient->isPlanConnected()) {
        emit errorOccurred("Cannot start mapper mission: VOXL Mapper plan socket is not connected");
        return;
    }

    emit messageReceived(QStringLiteral(
                             "Mapper start requested: %1 waypoint(s), mapper pose %2, plan socket connected.")
                             .arg(m_mapperMission.size())
                             .arg(m_haveMapperPose ? QStringLiteral("available") : QStringLiteral("not received yet")));
    m_missionActive = true;
    m_missionPaused = false;
    m_currentMissionItem = 0;
    m_resumeMissionItem = 0;
    m_mapperMissionState = MapperMissionState::Planning;
    m_mapperStateTimer.restart();
    m_mapperDebugTimer.restart();
    if (m_mapperMissionTimer)
        m_mapperMissionTimer->start();
    commandCurrentMapperWaypoint();

    emit missionStatusChanged("Mapper mission started");
    emit messageReceived(QStringLiteral("Mapper mission execution started at waypoint 1/%1 (no home leg).")
                             .arg(m_mapperMission.size()));
    emit messageReceived(QStringLiteral("Mapper mission command sequence: plan_to first waypoint, then follow_path when mapper returns a plan."));
}

void DroneController::pauseMission()
{
    if (!m_connected || !m_missionActive) {
        return;
    }
    
    if (m_mapperClient && m_mapperClient->isPlanConnected())
        m_mapperClient->stopFollowing();
    m_resumeMissionItem = m_currentMissionItem;
    m_mapperMissionState = MapperMissionState::Paused;
    m_missionPaused = true;
    emit missionStatusChanged("Mapper mission paused");
    emit messageReceived(QStringLiteral("Mapper mission paused at waypoint %1/%2.")
                             .arg(m_currentMissionItem + 1)
                             .arg(m_mapperMission.size()));
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
    if (!m_mapperClient || !m_mapperClient->isPlanConnected()) {
        emit errorOccurred("Cannot resume mapper mission: VOXL Mapper plan socket is not connected");
        return;
    }

    m_currentMissionItem = qBound(0, m_resumeMissionItem, m_mapperMission.size() - 1);
    m_missionActive = true;
    m_missionPaused = false;
    m_mapperMissionState = MapperMissionState::Planning;
    m_mapperStateTimer.restart();
    m_mapperDebugTimer.restart();
    if (m_mapperMissionTimer)
        m_mapperMissionTimer->start();
    commandCurrentMapperWaypoint();
    emit missionStatusChanged("Mapper mission resumed");
    emit messageReceived(QStringLiteral("Mapper mission resumed from waypoint %1/%2.")
                             .arg(m_currentMissionItem + 1)
                             .arg(m_mapperMission.size()));
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
    m_mapperPlanReceivedForCurrentTarget = false;
    m_mapperFollowIssuedForCurrentTarget = false;
    m_mapperPlanMismatchWarnedForCurrentTarget = false;
    
    if (m_connected) {
        if (m_mapperClient && m_mapperClient->isPlanConnected())
            m_mapperClient->stopFollowing();
    }
    
    emit missionStatusChanged("Mission cleared");
}

void DroneController::clearMapperMap()
{
    if (!m_mapperClient || m_droneHost.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot clear map: not connected to VOXL Mapper"));
        return;
    }
    if (m_mapperClient->isMeshConnected()) {
        // Send the restart POST — this is what VOXL Portal's Clear Map button
        // actually does (POST :8099/restart-voxl-mapper). The WebSocket sockets
        // will drop and auto-reconnect as voxl-mapper comes back up.
        m_mapperClient->restartMapper();
        emit messageReceived(QStringLiteral("VOXL Mapper restart requested (clear map)"));
    } else {
        // Socket is down — reconnect and dispatch the command when it comes up.
        m_pendingMapperMapCommand = PendingMapperMapCommand::Clear;
        emit messageReceived(QStringLiteral("Reconnecting VOXL Mapper mesh before clear..."));
        m_mapperClient->reconnectMesh();
    }
}

void DroneController::loadMapperMap(const QString &remotePath)
{
    const QString path = VoxlMapperPaths::normalizeSubdir(remotePath);
    if (!path.isEmpty() && path != remotePath.trimmed()) {
        emit warningIssued(QStringLiteral("Mapper path must be missions/<room>/ (got: %1)").arg(remotePath.trimmed()));
    }
    if (!m_mapperClient || !m_mapperClient->isMeshConnected()) {
        if (m_mapperClient && !m_droneHost.trimmed().isEmpty()) {
            m_pendingMapperMapCommand = PendingMapperMapCommand::Load;
            m_pendingMapperMapPath = path;
            m_mapperClient->connectToMapper(m_droneHost, 80);
            emit messageReceived("Connecting to VOXL Mapper mesh socket before loading map...");
        } else {
            emit errorOccurred("Cannot load map: VOXL Mapper is not connected");
        }
        return;
    }
    m_mapperClient->loadMap(path);
    emit messageReceived(path.isEmpty()
                             ? QStringLiteral("VOXL Mapper load default map command sent")
                             : QStringLiteral("VOXL Mapper load map command sent: %1").arg(path));
}

void DroneController::replaceMapperMap(const QString &remotePath)
{
    const QString path = VoxlMapperPaths::normalizeSubdir(remotePath);
    if (path.isEmpty() && !remotePath.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("Mapper path must be missions/<room>/ (got: %1)").arg(remotePath.trimmed()));
        return;
    }
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
    const QString path = VoxlMapperPaths::normalizeSubdir(remotePath);
    if (!m_mapperClient || !m_mapperClient->isMeshConnected()) {
        if (m_mapperClient && !m_droneHost.trimmed().isEmpty()) {
            m_pendingMapperMapCommand = PendingMapperMapCommand::Save;
            m_pendingMapperMapPath = path;
            m_pendingMapperMapFormat = format;
            m_mapperClient->connectToMapper(m_droneHost, 80);
            emit messageReceived("Connecting to VOXL Mapper mesh socket before saving map...");
        } else {
            emit errorOccurred("Cannot save map: VOXL Mapper is not connected");
        }
        return;
    }
    m_mapperClient->saveMap(format, path);
    emit messageReceived(path.isEmpty()
                             ? QStringLiteral("VOXL Mapper save map command sent")
                             : QStringLiteral("VOXL Mapper save map command sent: %1").arg(path));
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

void DroneController::listMapperMissionRooms()
{
    if (!m_connected || !m_voxlConnection) {
        emit mapperMissionRoomsListed({});
        return;
    }

    m_sshKindQueue.enqueue(PendingSshCommand::ListMissionRooms);
    m_sshPollSubdirQueue.enqueue(QString());
    m_voxlConnection->sshRunCommand(VoxlMapperPaths::listMissionRoomsScript());
}

void DroneController::syncMapperRoomState(const QString &mapperSubdir)
{
    const QString subdir = VoxlMapperPaths::normalizeSubdir(mapperSubdir);
    if (subdir.isEmpty()) {
        emit mapperMapPresencePolled(QString(), false, false);
        return;
    }
    if (!m_connected || !m_voxlConnection || m_droneHost.trimmed().isEmpty()) {
        emit mapperMapPresencePolled(subdir, false, false);
        return;
    }

    const int queuedSshCount = qMin(m_sshKindQueue.size(), m_sshPollSubdirQueue.size());
    for (int i = 0; i < queuedSshCount; ++i) {
        if (m_sshKindQueue.at(i) == PendingSshCommand::SyncMapperRoom
            && m_sshPollSubdirQueue.at(i) == subdir) {
            return;
        }
    }

    m_sshKindQueue.enqueue(PendingSshCommand::SyncMapperRoom);
    m_sshPollSubdirQueue.enqueue(subdir);
    m_voxlConnection->sshRunCommand(VoxlMapperPaths::listRoomsAndPollMapScript(subdir));
}

void DroneController::onSshCommandFinished(bool success, const QString &output)
{
    if (m_sshKindQueue.isEmpty() || m_sshPollSubdirQueue.isEmpty())
        return;

    const PendingSshCommand command = m_sshKindQueue.dequeue();
    const QString pollSubdir = m_sshPollSubdirQueue.dequeue();

    switch (command) {
    case PendingSshCommand::ListMissionRooms: {
        QStringList rooms;
        if (success) {
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty() && trimmed != QLatin1String(".") && trimmed != QLatin1String(".."))
                    rooms.append(trimmed);
            }
        }
        emit mapperMissionRoomsListed(rooms);
        break;
    }
    case PendingSshCommand::SyncMapperRoom: {
        QStringList rooms;
        bool present = false;
        bool mapLineSeen = false;
        if (success) {
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString trimmed = line.trimmed();
                if (trimmed == QLatin1String("MAP_READY")) {
                    present = true;
                    mapLineSeen = true;
                } else if (trimmed == QLatin1String("MAP_WAIT")) {
                    present = false;
                    mapLineSeen = true;
                } else if (trimmed.startsWith(QLatin1String("ROOM:"))) {
                    const QString room = trimmed.mid(5).trimmed();
                    if (!room.isEmpty() && room != QLatin1String(".") && room != QLatin1String(".."))
                        rooms.append(room);
                }
            }
        }
        if (success && mapLineSeen) {
            emit mapperMissionRoomsListed(rooms);
            emit mapperMapPresencePolled(pollSubdir, present, true);
        } else {
            if (!output.trimmed().isEmpty())
                emit warningIssued(QStringLiteral("Map check SSH failed for %1: %2").arg(pollSubdir, output));
            emit mapperMapPresencePolled(pollSubdir, false, false);
        }
        break;
    }
    case PendingSshCommand::ServiceMessage:
    case PendingSshCommand::None:
        if (success)
            emit messageReceived(output.isEmpty() ? QStringLiteral("SSH command succeeded")
                                                  : QStringLiteral("SSH: %1").arg(output));
        else
            emit warningIssued(QStringLiteral("SSH command failed: %1").arg(output));
        break;
    }
}

void DroneController::onMapperMeshConnectedChanged(bool connected)
{
    if (connected) {
        stopMapperPortalWatchdog();
    } else if (m_connected && m_mapperClient && m_mapperClient->isPlanConnected()) {
        // Mesh socket dropped while plan+pose are still up (this is the common
        // case when voxl-portal's /mesh endpoint resets after a clear_map or a
        // brief service hiccup).  Reconnect silently after 3 s so map streaming
        // resumes without requiring user intervention and without affecting the
        // in-progress mission on the plan socket.
        emit messageReceived(QStringLiteral(
            "VOXL Mapper mesh socket dropped — reconnecting in 3 s (mission unaffected)"));
        QTimer::singleShot(3000, this, [this]() {
            if (m_connected && m_mapperClient && !m_mapperClient->isMeshConnected())
                m_mapperClient->reconnectMesh();
        });
    }

    if (!connected || m_pendingMapperMapCommand == PendingMapperMapCommand::None)
        return;

    const PendingMapperMapCommand command = m_pendingMapperMapCommand;
    const QString path = m_pendingMapperMapPath;
    const QString format = m_pendingMapperMapFormat;
    m_pendingMapperMapCommand = PendingMapperMapCommand::None;
    m_pendingMapperMapPath.clear();
    m_pendingMapperMapFormat.clear();

    switch (command) {
    case PendingMapperMapCommand::Load:
        loadMapperMap(path);
        break;
    case PendingMapperMapCommand::Save:
        saveMapperMap(format.isEmpty() ? QStringLiteral("ply") : format, path);
        break;
    case PendingMapperMapCommand::Clear:
        // Call clearMap() directly here — do NOT call clearMapperMap() which
        // would queue another reconnect and loop indefinitely.
        m_mapperClient->clearMap();
        emit messageReceived(QStringLiteral("VOXL Mapper clear map command sent"));
        break;
    case PendingMapperMapCommand::LoadAfterClear:
        replaceMapperMap(path);
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

void DroneController::onThermalPollTimer()
{
    if (!m_connected || !m_voxlConnection)
        return;

    // One SSH call: thermal zones + voxl-inspect-services (top 3 by CPU).
    // Output lines:
    //   THERMAL:<type>:<raw_millidegC>
    //   SVC:<pct>:<name>         (sorted descending, max 3)
    //
    // Using voxl-inspect-services instead of `ps` because ps on VOXL2
    // BusyBox does not support --no-headers / --sort flags.
    // awk condition: Running field must be exactly 'Running' (not 'Not Running'),
    // and the CPU field must contain a digit.
    const QString cmd =
        QStringLiteral(
            "for z in /sys/class/thermal/thermal_zone*; do "
            "  t=$(cat $z/type 2>/dev/null); "
            "  v=$(cat $z/temp 2>/dev/null); "
            "  echo \"THERMAL:$t:$v\"; "
            "done; "
            "voxl-inspect-services 2>/dev/null | "
            "awk -F'|' '($3~/^ *Running *$/)&&($4~/[0-9]){"
            "gsub(/[ %]/,\"\",$4);gsub(/^ +| +$/,\"\",$1);"
            "if($4+0>0)printf \"SVC:%.1f:%s\\n\",$4+0,$1}' | "
            "sort -t: -k2 -rn | head -3");

    QProcess *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
                // Accept exit code 0 or 1 (voxl-inspect-services can exit 1 on
                // some firmware versions even when it produced valid output).
                const QString out = QString::fromUtf8(proc->readAllStandardOutput());
                float bestCpuTemp = m_currentStatus.voxlTempC; // keep last good value on empty
                QStringList svcLines;

                for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                    if (line.startsWith(QLatin1String("THERMAL:"))) {
                        const QStringList parts = line.split(QLatin1Char(':'));
                        if (parts.size() == 3) {
                            const QString &type = parts[1];
                            bool ok = false;
                            const float tempC = parts[2].trimmed().toFloat(&ok) / 1000.0f;
                            if (ok && type.contains(QLatin1String("cpu"), Qt::CaseInsensitive)) {
                                if (tempC > bestCpuTemp)
                                    bestCpuTemp = tempC;
                            }
                        }
                    } else if (line.startsWith(QLatin1String("SVC:"))) {
                        // Format: SVC:<pct>:<service-name>
                        const int first  = line.indexOf(QLatin1Char(':'));
                        const int second = line.indexOf(QLatin1Char(':'), first + 1);
                        if (first != -1 && second != -1) {
                            bool ok = false;
                            const float pct = line.mid(first + 1, second - first - 1).toFloat(&ok);
                            if (ok && pct > 0.0f) {
                                QString name = line.mid(second + 1).trimmed();
                                // Shorten: remove "voxl-" prefix and "-server" suffix
                                if (name.startsWith(QLatin1String("voxl-")))
                                    name.remove(0, 5);
                                if (name.endsWith(QLatin1String("-server")))
                                    name.chop(7);
                                if (name.length() > 12)
                                    name = name.left(11) + QLatin1Char('+');
                                svcLines.append(QStringLiteral("%1:%2%")
                                    .arg(name).arg(pct, 0, 'f', 0));
                            }
                        }
                    }
                }

                m_currentStatus.voxlTempC     = bestCpuTemp;
                m_currentStatus.voxlTopService = svcLines.join(QStringLiteral("  "));
                emit statusUpdated(m_currentStatus);
                proc->deleteLater();
            });
    connect(proc, &QProcess::errorOccurred, this, [proc](QProcess::ProcessError) {
        proc->deleteLater();
    });

    const QStringList args = {
        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
        QStringLiteral("-o"), QStringLiteral("UserKnownHostsFile=/dev/null"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5"),
        QStringLiteral("root@") + m_droneHost,
        cmd
    };
    proc->start(QStringLiteral("ssh"), args);
}

void DroneController::resetVioService(const QString &serviceName)
{
    if (!m_mapperClient || m_droneHost.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot reset VIO: not connected to VOXL Mapper"));
        return;
    }
    // VOXL Portal vio.js resets each service by opening a NEW WebSocket
    // connection to /reset_qvio/ or /reset_ov/ — the connection itself
    // triggers the reset.  No message is sent and the /mesh socket is not
    // involved, so no reconnect step is needed.
    if (serviceName == QLatin1String("voxl-qvio-server")) {
        m_mapperClient->resetQvio();
        emit messageReceived(QStringLiteral("QVIO reset sent to drone (/reset_qvio/)"));
    } else if (serviceName == QLatin1String("voxl-open-vins-server")) {
        m_mapperClient->resetOv();
        emit messageReceived(QStringLiteral("OV Extended reset sent to drone (/reset_ov/)"));
    } else if (serviceName == QLatin1String("voxl-mapper")) {
        if (!m_voxlConnection || m_droneHost.trimmed().isEmpty()) {
            emit errorOccurred(QStringLiteral("Cannot restart mapper: not connected to VOXL"));
            return;
        }
        m_sshKindQueue.enqueue(PendingSshCommand::ServiceMessage);
        m_sshPollSubdirQueue.enqueue(QString());
        m_voxlConnection->sshRunCommand(
            QStringLiteral("systemctl restart voxl-mapper && echo 'voxl-mapper restarted successfully'"));
        emit messageReceived(QStringLiteral("Restarting voxl-mapper service via SSH..."));
    } else {
        emit errorOccurred(QStringLiteral("Unknown VIO service: %1").arg(serviceName));
    }
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
    m_currentStatus.positionIsMapperLocal = true;
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
    if (!points.isEmpty()) {
        emit messageReceived(QStringLiteral("VOXL Mapper render update: %1 points from %2")
                                 .arg(points.size())
                                 .arg(name.trimmed().isEmpty() ? QStringLiteral("mesh/plan") : name.trimmed()));
        if (m_missionActive && !m_missionPaused && m_mapperMissionState == MapperMissionState::Planning) {
            if (m_currentMissionItem < 0 || m_currentMissionItem >= m_mapperMission.size())
                return;
            const MapperMissionWaypoint &target = m_mapperMission.at(m_currentMissionItem);
            float closestPointToTargetM = std::numeric_limits<float>::max();
            for (const MapperPathPoint &point : points)
                closestPointToTargetM = qMin(closestPointToTargetM, (point.positionFrd - target.frdPosition).length());

            // Inform about plan quality but never block follow_path on render distance.
            // The mapper always plans as close as it can — blocking here causes the drone
            // to hang on a waypoint whenever the mapper can't reach it exactly.
            emit messageReceived(QStringLiteral(
                "Mapper plan received for waypoint %1/%2: %3 pts, closest point %4 m from target.")
                    .arg(m_currentMissionItem + 1)
                    .arg(m_mapperMission.size())
                    .arg(points.size())
                    .arg(closestPointToTargetM, 0, 'f', 2));

            m_mapperPlanReceivedForCurrentTarget = true;
            issueMapperFollowForCurrentWaypoint(QStringLiteral("%1 pt plan from %2, closest %3 m to target")
                                                    .arg(points.size())
                                                    .arg(name.trimmed().isEmpty() ? QStringLiteral("unnamed plan") : name.trimmed())
                                                    .arg(closestPointToTargetM, 0, 'f', 2));
        }
    } else if (m_missionActive && m_mapperMissionState == MapperMissionState::Planning) {
        emit warningIssued(QStringLiteral("VOXL Mapper returned an empty plan render while planning waypoint %1/%2.")
                               .arg(m_currentMissionItem + 1)
                               .arg(m_mapperMission.size()));
    }
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
        if (!m_mapperPlanReceivedForCurrentTarget && m_mapperDebugTimer.elapsed() > 1000) {
            emit messageReceived(QStringLiteral(
                                     "Mapper debug: waiting for plan render for waypoint %1/%2; target FRD (%3, %4, %5).")
                                     .arg(m_currentMissionItem + 1)
                                     .arg(m_mapperMission.size())
                                     .arg(target.frdPosition.x(), 0, 'f', 2)
                                     .arg(target.frdPosition.y(), 0, 'f', 2)
                                     .arg(target.frdPosition.z(), 0, 'f', 2));
            m_mapperDebugTimer.restart();
        }
        // After 3 s with no plan render, re-send plan_to in case the mapper dropped it.
        // After 6 s still nothing, force follow_path anyway so the mission isn't blocked.
        if (!m_mapperPlanReceivedForCurrentTarget) {
            const qint64 elapsed = m_mapperStateTimer.elapsed();
            if (elapsed > 6000) {
                emit warningIssued(QStringLiteral(
                    "Mapper waypoint %1/%2: no plan render after %3 s — forcing follow_path.")
                        .arg(m_currentMissionItem + 1).arg(m_mapperMission.size())
                        .arg(elapsed / 1000.0, 0, 'f', 1));
                issueMapperFollowForCurrentWaypoint(
                    QStringLiteral("forced follow_path after %1 ms with no render").arg(elapsed));
            } else if (elapsed > 3000 && (elapsed / 3000) > ((elapsed - 200) / 3000)) {
                // Re-send plan_to once at the 3 s mark to recover from a dropped command.
                emit messageReceived(QStringLiteral(
                    "Mapper waypoint %1/%2: retrying plan_to at %3 s.")
                        .arg(m_currentMissionItem + 1).arg(m_mapperMission.size())
                        .arg(elapsed / 1000.0, 0, 'f', 1));
                m_mapperClient->planToFrd(target.frdPosition);
            }
        }
        return;
    }

    if (m_mapperMissionState == MapperMissionState::Following) {
        if (!m_haveMapperPose) {
            if (m_mapperDebugTimer.elapsed() > 1000) {
                emit warningIssued("Waiting for VOXL Mapper pose before waypoint arrival checks.");
                m_mapperDebugTimer.restart();
            }
            return;
        }

        const float distance = (m_mapperPositionFrd - target.frdPosition).length();
        const float speed = m_mapperVelocityFrd.length();
        if (m_mapperDebugTimer.elapsed() > 1000) {
            emit messageReceived(QStringLiteral(
                                     "Mapper debug: waypoint %1/%2 distance %3 m, speed %4 m/s, current FRD (%5, %6, %7), target FRD (%8, %9, %10).")
                                     .arg(m_currentMissionItem + 1)
                                     .arg(m_mapperMission.size())
                                     .arg(distance, 0, 'f', 2)
                                     .arg(speed, 0, 'f', 2)
                                     .arg(m_mapperPositionFrd.x(), 0, 'f', 2)
                                     .arg(m_mapperPositionFrd.y(), 0, 'f', 2)
                                     .arg(m_mapperPositionFrd.z(), 0, 'f', 2)
                                     .arg(target.frdPosition.x(), 0, 'f', 2)
                                     .arg(target.frdPosition.y(), 0, 'f', 2)
                                     .arg(target.frdPosition.z(), 0, 'f', 2));
            m_mapperDebugTimer.restart();
        }
        if (distance <= target.acceptanceRadiusM && speed < 0.8f) {
            emit messageReceived(QStringLiteral(
                                     "Mapper waypoint %1/%2 acceptance reached: distance %3 m <= %4 m, speed %5 m/s.")
                                     .arg(m_currentMissionItem + 1)
                                     .arg(m_mapperMission.size())
                                     .arg(distance, 0, 'f', 2)
                                     .arg(target.acceptanceRadiusM, 0, 'f', 2)
                                     .arg(speed, 0, 'f', 2));
            if (target.holdTimeSec > 0.0f) {
                m_mapperMissionState = MapperMissionState::Holding;
                m_mapperHoldTimer.restart();
                emit missionStatusChanged(QString("Holding waypoint %1 for %2 s")
                                              .arg(m_currentMissionItem + 1)
                                              .arg(target.holdTimeSec, 0, 'f', 1));
                emit messageReceived(QStringLiteral("Mapper waypoint %1 hold started for %2 s.")
                                         .arg(m_currentMissionItem + 1)
                                         .arg(target.holdTimeSec, 0, 'f', 1));
            } else {
                advanceMapperMission();
            }
        } else {
            // Periodic progress warning every 30 s.
            if (m_mapperStateTimer.elapsed() > 30000) {
                emit warningIssued(QStringLiteral(
                    "Mapper waypoint %1/%2 still not reached after %3 s (distance %4 m, speed %5 m/s).")
                        .arg(m_currentMissionItem + 1)
                        .arg(m_mapperMission.size())
                        .arg(m_waypointTotalTimer.elapsed() / 1000)
                        .arg((m_mapperPositionFrd - target.frdPosition).length(), 0, 'f', 2)
                        .arg(m_mapperVelocityFrd.length(), 0, 'f', 2));
                m_mapperStateTimer.restart();
            }
            // Hard abort if the waypoint has not been reached within the safety timeout.
            // This prevents the drone from hanging indefinitely on a single leg when
            // VIO drifts or an obstacle blocks the path permanently.
            constexpr qint64 kWaypointAbortTimeoutMs = 90000; // 90 s per waypoint
            if (m_waypointTotalTimer.elapsed() > kWaypointAbortTimeoutMs) {
                finishMapperMission(QStringLiteral(
                    "Mission aborted: waypoint %1/%2 not reached within %3 s safety timeout.")
                        .arg(m_currentMissionItem + 1)
                        .arg(m_mapperMission.size())
                        .arg(kWaypointAbortTimeoutMs / 1000));
                return;
            }
        }
        return;
    }

    if (m_mapperMissionState == MapperMissionState::Holding) {
        if (m_mapperHoldTimer.elapsed() >= static_cast<qint64>(target.holdTimeSec * 1000.0f)) {
            emit messageReceived(QStringLiteral("Mapper waypoint %1 hold complete.")
                                     .arg(m_currentMissionItem + 1));
            advanceMapperMission();
        }
    }
}

void DroneController::advanceMapperMission()
{
    const int completedWaypoint = m_currentMissionItem + 1;
    ++m_currentMissionItem;
    if (m_currentMissionItem >= m_mapperMission.size()) {
        finishMapperMission(QStringLiteral("Mapper mission complete"));
        return;
    }
    // Do NOT send stop_following here — it puts the mapper into an idle/hover
    // state that can prevent the next plan_to from being executed.  The new
    // plan_to command naturally supersedes the current trajectory on the mapper
    // side, so issuing stop_following between legs is unnecessary and harmful.
    emit messageReceived(QStringLiteral("Mapper advancing from waypoint %1 to %2/%3.")
                             .arg(completedWaypoint)
                             .arg(m_currentMissionItem + 1)
                             .arg(m_mapperMission.size()));
    commandCurrentMapperWaypoint();
}

void DroneController::commandCurrentMapperWaypoint()
{
    if (!m_mapperClient || !m_mapperClient->isPlanConnected())
        return;
    if (m_currentMissionItem < 0 || m_currentMissionItem >= m_mapperMission.size())
        return;

    const MapperMissionWaypoint &target = m_mapperMission[m_currentMissionItem];
    m_mapperPlanReceivedForCurrentTarget = false;
    m_mapperFollowIssuedForCurrentTarget = false;
    m_mapperPlanMismatchWarnedForCurrentTarget = false;
    m_mapperClient->planToFrd(target.frdPosition);
    m_mapperMissionState = MapperMissionState::Planning;
    m_mapperStateTimer.restart();
    m_mapperDebugTimer.restart();
    m_waypointTotalTimer.restart();
    emit missionStatusChanged(QString("Planning mapper waypoint %1/%2: FRD (%3, %4, %5)")
                                  .arg(m_currentMissionItem + 1)
                                  .arg(m_mapperMission.size())
                                  .arg(target.frdPosition.x(), 0, 'f', 2)
                                  .arg(target.frdPosition.y(), 0, 'f', 2)
                                  .arg(target.frdPosition.z(), 0, 'f', 2));
    emit messageReceived(QStringLiteral(
                             "Mapper command: plan_to waypoint %1/%2 logical (%3, %4, %5) -> FRD (%6, %7, %8), acceptance %9 m, hold %10 s.")
                             .arg(m_currentMissionItem + 1)
                             .arg(m_mapperMission.size())
                             .arg(target.logicalPosition.x(), 0, 'f', 2)
                             .arg(target.logicalPosition.y(), 0, 'f', 2)
                             .arg(target.logicalPosition.z(), 0, 'f', 2)
                             .arg(target.frdPosition.x(), 0, 'f', 2)
                             .arg(target.frdPosition.y(), 0, 'f', 2)
                             .arg(target.frdPosition.z(), 0, 'f', 2)
                             .arg(target.acceptanceRadiusM, 0, 'f', 2)
                             .arg(target.holdTimeSec, 0, 'f', 1));
    if (m_haveMapperPose) {
        emit messageReceived(QStringLiteral("Mapper debug: current FRD before plan_to (%1, %2, %3), distance to target %4 m.")
                                 .arg(m_mapperPositionFrd.x(), 0, 'f', 2)
                                 .arg(m_mapperPositionFrd.y(), 0, 'f', 2)
                                 .arg(m_mapperPositionFrd.z(), 0, 'f', 2)
                                 .arg((m_mapperPositionFrd - target.frdPosition).length(), 0, 'f', 2));
    } else {
        emit warningIssued(QStringLiteral("Mapper debug: no pose received yet when starting waypoint %1.").arg(m_currentMissionItem + 1));
    }
}

void DroneController::issueMapperFollowForCurrentWaypoint(const QString &reason)
{
    if (m_mapperFollowIssuedForCurrentTarget)
        return;
    if (!m_mapperClient || !m_mapperClient->isPlanConnected())
        return;
    if (m_currentMissionItem < 0 || m_currentMissionItem >= m_mapperMission.size())
        return;

    m_mapperFollowIssuedForCurrentTarget = true;
    m_mapperClient->followPath();
    m_mapperMissionState = MapperMissionState::Following;
    m_mapperStateTimer.restart();
    m_mapperDebugTimer.restart();
    emit missionStatusChanged(QString("Following mapper waypoint %1/%2")
                                  .arg(m_currentMissionItem + 1)
                                  .arg(m_mapperMission.size()));
    emit messageReceived(QStringLiteral("Mapper command: follow_path for waypoint %1/%2 (%3).")
                             .arg(m_currentMissionItem + 1)
                             .arg(m_mapperMission.size())
                             .arg(reason));
}

void DroneController::logMapperMissionSummary(const QString &context)
{
    if (m_mapperMission.isEmpty()) {
        emit messageReceived(QStringLiteral("%1: no mapper waypoints staged.").arg(context));
        return;
    }

    constexpr int kMaxLoggedWaypoints = 12;
    const int missionSize = static_cast<int>(m_mapperMission.size());
    emit messageReceived(QStringLiteral("%1: %2 waypoint(s). Planner logical frame is X forward, Y left, Z up; VOXL Mapper command frame is X forward, Y right, Z down.")
                             .arg(context)
                             .arg(missionSize));
    const int countToLog = qMin(kMaxLoggedWaypoints, missionSize);
    for (int i = 0; i < countToLog; ++i) {
        const MapperMissionWaypoint &wp = m_mapperMission.at(i);
        emit messageReceived(QStringLiteral(
                                 "  wp %1/%2 seq %3: logical (%4, %5, %6), FRD (%7, %8, %9), yaw %10 deg, acceptance %11 m, hold %12 s.")
                                 .arg(i + 1)
                                 .arg(missionSize)
                                 .arg(wp.sequence)
                                 .arg(wp.logicalPosition.x(), 0, 'f', 2)
                                 .arg(wp.logicalPosition.y(), 0, 'f', 2)
                                 .arg(wp.logicalPosition.z(), 0, 'f', 2)
                                 .arg(wp.frdPosition.x(), 0, 'f', 2)
                                 .arg(wp.frdPosition.y(), 0, 'f', 2)
                                 .arg(wp.frdPosition.z(), 0, 'f', 2)
                                 .arg(wp.yawDeg, 0, 'f', 1)
                                 .arg(wp.acceptanceRadiusM, 0, 'f', 2)
                                 .arg(wp.holdTimeSec, 0, 'f', 1));
    }
    if (missionSize > kMaxLoggedWaypoints) {
        emit messageReceived(QStringLiteral("  ... %1 additional waypoint(s) not listed.")
                                 .arg(missionSize - kMaxLoggedWaypoints));
    }
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
    m_mapperPlanReceivedForCurrentTarget = false;
    m_mapperFollowIssuedForCurrentTarget = false;
    m_mapperPlanMismatchWarnedForCurrentTarget = false;
    emit missionStatusChanged(message);
    emit messageReceived(message);
}

// ── Trajectory helpers ────────────────────────────────────────────────────────

QString DroneController::writeTrajectoryFile(const Trajectory &traj)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/trajectories");
    if (!QDir().mkpath(dir)) {
        const QString reason = QStringLiteral("Failed to create trajectories directory: %1").arg(dir);
        qDebug() << "[DroneController]" << reason;
        emit trajectoryUploadFailed(reason);
        return QString();
    }

    const QString filename = QString("traj_%1.json")
                                 .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString localPath = dir + QStringLiteral("/") + filename;

    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString reason = QStringLiteral("Failed to write trajectory file: %1").arg(localPath);
        qDebug() << "[DroneController]" << reason;
        emit trajectoryUploadFailed(reason);
        return QString();
    }
    file.write(QJsonDocument(traj.toJson()).toJson(QJsonDocument::Indented));
    file.close();

    m_lastTrajectoryPath = localPath;
    qDebug() << "[DroneController] Trajectory written to" << localPath;
    return localPath;
}

void DroneController::uploadTrajectory(const Trajectory &traj)
{
    // Validate first.
    const QStringList errors = traj.validate();
    if (!errors.isEmpty()) {
        qDebug() << "[DroneController] Trajectory validation failed:" << errors;
        emit trajectoryValidationFailed(errors);
        return;
    }

    // Write persistent JSON file.
    const QString localPath = writeTrajectoryFile(traj);
    if (localPath.isEmpty())
        return; // writeTrajectoryFile already emitted trajectoryUploadFailed

    m_trajectoryActive = false;
    m_startingTrajectory = false;

    const QString summary = QStringLiteral("%1 samples · %2 s · %3 m · peak %4 m/s")
                                .arg(traj.samples().size())
                                .arg(traj.duration(), 0, 'f', 1)
                                .arg(traj.distance(), 0, 'f', 1)
                                .arg(traj.peakSpeedMs(), 0, 'f', 1);

    if (m_voxlConnection && m_connected) {
        qDebug() << "[DroneController] Uploading trajectory to VOXL:" << localPath;
        m_uploadingTrajectory = true;
        m_voxlConnection->uploadFileToVoxl(localPath,
                                           QStringLiteral("/data/trajectories/inbox/trajectory.json"),
                                           QStringLiteral("trajectory"));
        emit messageReceived(QStringLiteral("Uploading trajectory to VOXL: %1").arg(summary));
    } else {
        // Offline — stage locally only.
        qDebug() << "[DroneController] Offline: trajectory staged locally" << localPath;
        emit trajectoryStaged(localPath, summary);
        emit messageReceived(QStringLiteral("Trajectory staged locally (offline): %1").arg(summary));
    }
}

void DroneController::stageTrajectoryLocally(const Trajectory &traj)
{
    // Validate first.
    const QStringList errors = traj.validate();
    if (!errors.isEmpty()) {
        qDebug() << "[DroneController] Trajectory validation failed (stage):" << errors;
        emit trajectoryValidationFailed(errors);
        return;
    }

    const QString localPath = writeTrajectoryFile(traj);
    if (localPath.isEmpty())
        return;

    m_trajectoryActive = false;
    m_startingTrajectory = false;

    const QString summary = QStringLiteral("%1 samples · %2 s · %3 m · peak %4 m/s")
                                .arg(traj.samples().size())
                                .arg(traj.duration(), 0, 'f', 1)
                                .arg(traj.distance(), 0, 'f', 1)
                                .arg(traj.peakSpeedMs(), 0, 'f', 1);

    qDebug() << "[DroneController] Trajectory staged locally:" << localPath;
    emit trajectoryStaged(localPath, summary);
    emit messageReceived(QStringLiteral("Trajectory staged locally: %1").arg(summary));
}

void DroneController::cancelTrajectory()
{
    if (!m_trajectoryActive && !m_uploadingTrajectory && !m_startingTrajectory)
        return;

    m_uploadingTrajectory = false;
    m_startingTrajectory = false;
    m_trajectoryActive = false;

    emit trajectoryCancelled();
    emit missionStatusChanged(QStringLiteral("Trajectory cancelled"));
    emit messageReceived(QStringLiteral("Trajectory cancelled by user"));
}
