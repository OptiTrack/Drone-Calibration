#ifndef DRONESTATUSWIDGET_H
#define DRONESTATUSWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QVector3D>
#include <QProgressBar>
#include <QGroupBox>
#include <QPushButton>
#include <QTimer>
#include <QTextEdit>
#include <QListWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>

struct DroneStatus {
    bool connected;
    float batteryPercentage;
    float batteryVoltage;
    QString flightMode;
    bool armed;
    bool gpsLock;
    int gpsNumSats;
    float altitude;
    float groundSpeed;
    float verticalSpeed;
    QVector3D position;
    bool positionIsMapperLocal;
    QVector3D velocity;
    QVector3D attitude; // roll, pitch, yaw in degrees
    QString lastHeartbeat;
    QString systemStatus;
    QStringList errors;
    QStringList warnings;
    // System health (populated when connected)
    float px4LoadPercent = -1.0f;  // PX4 mainloop load %; -1 = unknown
    float voxlTempC      = -1.0f;  // VOXL2 CPU thermal zone °C; -1 = unknown
    QString voxlTopService;         // highest-CPU voxl service name+% (from SSH poll)
};

QT_BEGIN_NAMESPACE
namespace Ui { class DroneStatusWidget; }
QT_END_NAMESPACE

class DroneStatusWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DroneStatusWidget(QWidget *parent = nullptr);
    ~DroneStatusWidget();

    void updateDroneStatus(const DroneStatus &status);
    void setConnectionStatus(bool connected);
    bool addSystemMessage(const QString &message, const QString &type = "info");
    void onNavDllActReceived(float value);

signals:
    void armDisarmRequested(bool arm);
    void takeoffRequested();
    void landRequested();
    void returnToLaunchRequested();
    void forceDisarmRequested();
    void flightTerminationRequested();
    /// Emitted when user clicks "Configure RC-Only Arming"; carries param name + value.
    void setPx4ParameterRequested(const QString &paramId, float value, quint8 paramType);

private slots:
    void onArmDisarmClicked();
    void onTakeoffClicked();
    void onLandClicked();
    void onRTLClicked();
    void onForceDisarmClicked();
    void onFlightTerminationClicked();
    void onConfigureRcArmingClicked();
    void onStatusUpdateTimer();
    void onClearMessages();

private:
    void setupConnections();
    void updateBatteryDisplay();
    void updateFlightDisplay();
    void updatePositionDisplay();
    void updateControlsDisplay();
    void updateRcArmingButton();
    void addMessage(const QString &message, const QString &type = "info");
    QString formatCoordinate(float value, const QString &unit);
    
    Ui::DroneStatusWidget *ui;
    QCheckBox *m_hideMapperMeshMessagesCheckBox;
    QLabel *m_healthLabel;    // compact CPU/thermal bar shown at top of widget

    // Data and timers
    DroneStatus m_currentStatus;
    QTimer *m_statusUpdateTimer;
    int m_navDllAct; // -1 = unknown, 0 = RC-only, >0 = GCS required
};

#endif // DRONESTATUSWIDGET_H
