#ifndef DRONECONTROLLER_H
#define DRONECONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector3D>
#include <QColor>
#include <QJsonObject>
#include <vector>
#include "../widgets/dronestatuswidget.h"
#include "../models/waypoint.h"
#include "../network/voxlmapperclient.h"

class VOXLConnection;

struct MissionItem {
    int sequence;
    QString command;
    QVector3D position;
    float param1, param2, param3, param4;
    bool autocontinue;
};

struct FlightPlan {
    QString id;
    QString name;
    QVector<MissionItem> items;
    bool uploaded;
};

struct MapperMissionWaypoint {
    int sequence = 0;
    QVector3D logicalPosition; // Same as planner: X forward, Y left/lateral (green), Z up (blue).
    QVector3D frdPosition;     // VOXL Mapper command frame: X forward, Y right, Z down.
    float holdTimeSec = 0.0f;
    float acceptanceRadiusM = 0.5f;
    float yawDeg = 0.0f;
};

class DroneController : public QObject
{
    Q_OBJECT

public:
    explicit DroneController(QObject *parent = nullptr);
    ~DroneController();

    // Connection management
    bool connectToDrone(const QString &host = "192.168.1.10", int port = 14550);
    void disconnectFromDrone();
    bool isConnected() const { return m_connected; }
    QString voxlHost() const { return m_droneHost; }

    // Flight control
    void armDrone(bool arm);
    void takeoff(float altitude = 10.0f);
    void land();
    void returnToLaunch();
    /// MAVLink force-disarm (kill motors); reversible; does not cut vehicle power.
    void forceDisarm();
    /// PX4 flight termination (MAV_CMD_DO_FLIGHTTERMINATION); strongest software stop if enabled in firmware.
    void flightTermination();

    // Mission/waypoint control
    void uploadMission(const QVector<QVector3D> &waypoints);
    void uploadMapperMission(const std::vector<Waypoint> &waypoints);
    /// Mark mission as uploaded locally without sending commands (UI / SIL testing when disconnected).
    void stageMissionLocally(const QVector<QVector3D> &waypoints);
    void stageMapperMissionLocally(const std::vector<Waypoint> &waypoints);
    void startMission();
    void pauseMission();
    void resumeMission();
    void abortMission();
    void clearMission();
    void clearMapperMap();
    void loadMapperMap(const QString &remotePath = QString());
    /// clear_map on VOXL, then load_map after a short delay (avoids stacking two maps in mapper + garbled mesh in the UI).
    void replaceMapperMap(const QString &remotePath = QString());
    /// clear_map, scp-upload a saved local bundle folder to remotePath, then load_map (live /mesh stream resumes after load).
    void restoreMapperMapFromBundle(const QString &localBundleDir, const QString &remoteMapPath);
    void saveMapperMap(const QString &format = QStringLiteral("ply"), const QString &remotePath = QString());
    void downloadMapperMapFromVehicle(const QString &localDir, const QString &remotePath);
    void uploadMapperMap(const QString &localFilePath, const QString &remotePath);
    bool isMapperMeshConnected() const;
    void planMapperHome();

    // Manual control
    void setManualControl(float roll, float pitch, float yaw, float throttle);
    void setPositionTarget(const QVector3D &position);
    void setVelocityTarget(const QVector3D &velocity);

    // Camera control
    void startVideoRecording();
    void stopVideoRecording();
    void takePicture();
    void setCameraSettings(const QString &mode, int quality);

    // Getters
    DroneStatus getCurrentStatus() const { return m_currentStatus; }
    /// Last mapper VIO pose in planner logical frame (after plan_home, matches where H is placed).
    bool mapperPoseAvailableForPlanner(QVector3D &logicalOut, float &yawDegOut) const;
    FlightPlan getCurrentMission() const { return m_currentMission; }
    bool isMissionRunning() const { return m_missionActive; }
    bool isMissionPaused() const { return m_missionPaused; }

signals:
    void connectionStatusChanged(bool connected);
    void statusUpdated(const DroneStatus &status);
    void mapperPoseUpdated(const QVector3D &positionLogical, float yawDeg);
    void mapperRenderUpdated(const QVector<QVector3D> &positionsLogical, const QVector<QColor> &colors);
    void mapperMeshUpdated(const QVector<QVector3D> &positionsLogical, const QVector<QColor> &colors, const QVector<quint32> &triangleIndices);
    void missionStatusChanged(const QString &status);
    void errorOccurred(const QString &error);
    void warningIssued(const QString &warning);
    void messageReceived(const QString &message);
    void mapperBundleDownloadFinished(bool success, const QString &message);

private slots:
    void onHeartbeatTimer();
    void onStatusUpdateTimer();
    void onVOXLDataReceived(const QJsonObject &data);
    void onVOXLConnectionStatusChanged(bool connected);
    void onVOXLError(const QString &error);
    void onMapperPoseReceived(const QVector3D &positionFrd, const QVector3D &velocityFrd, float yawRad);
    void onMapperRenderReceived(const QString &name, int format, const QVector<MapperPathPoint> &points);
    void onMapperMeshReceived(const QVector<MapperPathPoint> &vertices, const QVector<quint32> &triangleIndices);
    void onMapperMeshConnectedChanged(bool connected);
    void onMapperTick();
    void onMapperPortalWatchdogTimeout();
    void onMapperBundleUploadForRestoreFinished(bool success, const QString &message);

private:
    void initializeConnection();
    void stopMapperPortalWatchdog();
    void requestStatus();
    void processStatusData(const QJsonObject &data);
    void processMissionStatus(const QJsonObject &data);
    void sendCommand(const QString &command, const QJsonObject &params = QJsonObject());
    void sendMavlinkCommand(int command, float param1 = 0, float param2 = 0, float param3 = 0, float param4 = 0, float param5 = 0, float param6 = 0, float param7 = 0);
    QVector<MissionItem> waypointsToMissionItems(const QVector<QVector3D> &waypoints);
    QVector<MapperMissionWaypoint> waypointsToMapperMission(const std::vector<Waypoint> &waypoints) const;
    static QVector3D logicalToMapperFrd(const QVector3D &logicalPosition);
    static QVector3D mapperFrdToLogical(const QVector3D &positionFrd);
    void advanceMapperMission();
    void commandCurrentMapperWaypoint();
    void issueMapperFollowForCurrentWaypoint(const QString &reason);
    void logMapperMissionSummary(const QString &context);
    void finishMapperMission(const QString &message);
    void updateConnectionStatus(bool connected);
    /// Stops VOXL Mapper follow_path and local sequencer so PX4 can own modes (Land, RTL, etc.).
    void releaseMapperTrajectoryToVehicle();
    
    // Connection
    VOXLConnection *m_voxlConnection;
    VOXLMapperClient *m_mapperClient;
    bool m_connected;
    QString m_droneHost;
    int m_dronePort;
    
    // Timers
    QTimer *m_heartbeatTimer;
    QTimer *m_statusUpdateTimer;
    
    // Status
    DroneStatus m_currentStatus;
    FlightPlan m_currentMission;
    
    // Software in the Loop settings
    bool m_silMode;
    QString m_silHost;
    int m_silPort;
    
    // Mission tracking
    int m_currentMissionItem;
    bool m_missionActive;
    bool m_missionPaused;
    enum class MapperMissionState {
        Idle,
        Planning,
        Following,
        Holding,
        Paused
    };
    QVector<MapperMissionWaypoint> m_mapperMission;
    MapperMissionState m_mapperMissionState;
    QTimer *m_mapperMissionTimer;
    QElapsedTimer m_mapperStateTimer;
    QElapsedTimer m_mapperHoldTimer;
    QElapsedTimer m_mapperDebugTimer;
    QVector3D m_mapperPositionFrd;
    QVector3D m_mapperVelocityFrd;
    bool m_haveMapperPose;
    bool m_mapperPlanReceivedForCurrentTarget;
    bool m_mapperFollowIssuedForCurrentTarget;
    bool m_mapperPlanMismatchWarnedForCurrentTarget;
    int m_resumeMissionItem;
    enum class PendingMapperMapCommand {
        None,
        Load,
        Save,
        Clear,
        LoadAfterClear,
        RestoreFromBundle
    };
    PendingMapperMapCommand m_pendingMapperMapCommand;
    QString m_pendingMapperMapPath;
    QString m_pendingMapperMapFormat;
    QString m_pendingMapperBundleLocalDir;
    QString m_pendingBundleRestoreRemote;
    QTimer *m_mapperPortalWatchdog;
    
    // Manual control state
    bool m_manualControlActive;
    QTimer *m_manualControlTimer;
};

#endif // DRONECONTROLLER_H
