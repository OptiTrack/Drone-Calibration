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
    void takeoffRequested();
    void landRequested();
    void returnToLaunchRequested();
    void forceDisarmRequested();
    void flightTerminationRequested();

private slots:
    void onArmDisarmClicked();
    void onTakeoffClicked();
    void onLandClicked();
    void onRTLClicked();
    void onForceDisarmClicked();
    void onFlightTerminationClicked();
    void onStatusUpdateTimer();
    void onClearMessages();

private:
    void setupConnections();
    void updateBatteryDisplay();
    void updateFlightDisplay();
    void updatePositionDisplay();
    void updateControlsDisplay();
    void addMessage(const QString &message, const QString &type = "info");
    QString formatCoordinate(float value, const QString &unit);
    
    Ui::DroneStatusWidget *ui;
    
    // Data and timers
    DroneStatus m_currentStatus;
    QTimer *m_statusUpdateTimer;
};

#endif // DRONESTATUSWIDGET_H