#ifndef DRONECONTROLLER_H
#define DRONECONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QVector3D>
#include <QJsonObject>
#include "../widgets/dronestatuswidget.h"

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

    // Flight control
    void armDrone(bool arm);
    void setFlightMode(const QString &mode);
    void takeoff(float altitude = 10.0f);
    void land();
    void returnToLaunch();
    void emergencyStop();

    // Mission/waypoint control
    void uploadMission(const QVector<QVector3D> &waypoints);
    void startMission();
    void pauseMission();
    void resumeMission();
    void abortMission();
    void clearMission();

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
    FlightPlan getCurrentMission() const { return m_currentMission; }

signals:
    void connectionStatusChanged(bool connected);
    void statusUpdated(const DroneStatus &status);
    void missionStatusChanged(const QString &status);
    void errorOccurred(const QString &error);
    void warningIssued(const QString &warning);
    void messageReceived(const QString &message);

private slots:
    void onHeartbeatTimer();
    void onStatusUpdateTimer();
    void onVOXLDataReceived(const QJsonObject &data);
    void onVOXLConnectionStatusChanged(bool connected);
    void onVOXLError(const QString &error);

private:
    void initializeConnection();
    void requestStatus();
    void processStatusData(const QJsonObject &data);
    void processMissionStatus(const QJsonObject &data);
    void sendCommand(const QString &command, const QJsonObject &params = QJsonObject());
    void sendMavlinkCommand(int command, float param1 = 0, float param2 = 0, float param3 = 0, float param4 = 0, float param5 = 0, float param6 = 0, float param7 = 0);
    QVector<MissionItem> waypointsToMissionItems(const QVector<QVector3D> &waypoints);
    void updateConnectionStatus(bool connected);
    
    // Connection
    VOXLConnection *m_voxlConnection;
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
    
    // Manual control state
    bool m_manualControlActive;
    QTimer *m_manualControlTimer;
};

#endif // DRONECONTROLLER_H