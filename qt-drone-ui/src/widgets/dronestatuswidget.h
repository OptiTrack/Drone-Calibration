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
    QVector3D velocity;
    QVector3D attitude; // roll, pitch, yaw in degrees
    QString lastHeartbeat;
    QString systemStatus;
    QStringList errors;
    QStringList warnings;
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

signals:
    void armDisarmRequested(bool arm);
    void flightModeChangeRequested(const QString &mode);
    void takeoffRequested();
    void landRequested();
    void returnToLaunchRequested();
    void emergencyStopRequested();

private slots:
    void onArmDisarmClicked();
    void onTakeoffClicked();
    void onLandClicked();
    void onRTLClicked();
    void onEmergencyStopClicked();
    void onFlightModeChanged(const QString &mode);
    void onStatusUpdateTimer();
    void onClearMessages();

private:
    void setupUI();
    void setupBatteryGroup();
    void setupFlightGroup();
    void setupPositionGroup();
    void setupControlsGroup();
    void setupMessagesGroup();
    void updateBatteryDisplay();
    void updateFlightDisplay();
    void updatePositionDisplay();
    void updateControlsDisplay();
    void addMessage(const QString &message, const QString &type = "info");
    QString formatCoordinate(float value, const QString &unit);
    
    Ui::DroneStatusWidget *ui;
    
    // Main layout
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_topLayout;
    QVBoxLayout *m_leftLayout;
    QVBoxLayout *m_rightLayout;
    
    // Battery group
    QGroupBox *m_batteryGroup;
    QVBoxLayout *m_batteryLayout;
    QLabel *m_batteryPercentageLabel;
    QProgressBar *m_batteryProgressBar;
    QLabel *m_batteryVoltageLabel;
    QLabel *m_batteryStatusLabel;
    
    // Flight status group
    QGroupBox *m_flightGroup;
    QGridLayout *m_flightLayout;
    QLabel *m_connectionStatusLabel;
    QLabel *m_flightModeLabel;
    QLabel *m_armedStatusLabel;
    QLabel *m_gpsStatusLabel;
    QLabel *m_altitudeLabel;
    QLabel *m_groundSpeedLabel;
    QLabel *m_verticalSpeedLabel;
    QLabel *m_systemStatusLabel;
    
    // Position group
    QGroupBox *m_positionGroup;
    QGridLayout *m_positionLayout;
    QLabel *m_latitudeLabel;
    QLabel *m_longitudeLabel;
    QLabel *m_altitudeAbsLabel;
    QLabel *m_rollLabel;
    QLabel *m_pitchLabel;
    QLabel *m_yawLabel;
    
    // Controls group
    QGroupBox *m_controlsGroup;
    QVBoxLayout *m_controlsLayout;
    QComboBox *m_flightModeCombo;
    QPushButton *m_armDisarmButton;
    QPushButton *m_takeoffButton;
    QPushButton *m_landButton;
    QPushButton *m_rtlButton;
    QPushButton *m_emergencyStopButton;
    
    // Messages group
    QGroupBox *m_messagesGroup;
    QVBoxLayout *m_messagesLayout;
    QListWidget *m_messagesList;
    QPushButton *m_clearMessagesButton;
    
    // Data and timers
    DroneStatus m_currentStatus;
    QTimer *m_statusUpdateTimer;
    QTimer *m_simulationTimer;
    
    // Simulation state (for demo purposes)
    bool m_simulationMode;
    float m_simBatteryLevel;
    bool m_simArmed;
    QString m_simFlightMode;
};

#endif // DRONESTATUSWIDGET_H