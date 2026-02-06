#include "dronestatuswidget.h"
#include "ui_dronestatuswidget.h"
#include <QDateTime>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QtMath>

DroneStatusWidget::DroneStatusWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DroneStatusWidget)
    , m_statusUpdateTimer(nullptr)
    , m_simulationTimer(nullptr)
    , m_simulationMode(true)
    , m_simBatteryLevel(85.0f)
    , m_simArmed(false)
    , m_simFlightMode("STABILIZE")
{
    ui->setupUi(this);
    ui->setupUi(this);
    
    // Initialize status
    m_currentStatus.connected = false;
    m_currentStatus.batteryPercentage = 0.0f;
    m_currentStatus.batteryVoltage = 0.0f;
    m_currentStatus.flightMode = "UNKNOWN";
    m_currentStatus.armed = false;
    m_currentStatus.gpsLock = false;
    m_currentStatus.gpsNumSats = 0;
    m_currentStatus.altitude = 0.0f;
    m_currentStatus.groundSpeed = 0.0f;
    m_currentStatus.verticalSpeed = 0.0f;
    m_currentStatus.position = QVector3D(0, 0, 0);
    m_currentStatus.velocity = QVector3D(0, 0, 0);
    m_currentStatus.attitude = QVector3D(0, 0, 0);
    m_currentStatus.systemStatus = "STANDBY";
    
    setupConnections();
    
    // Set up timers
    m_statusUpdateTimer = new QTimer(this);
    m_statusUpdateTimer->setInterval(1000); // Update every second
    connect(m_statusUpdateTimer, &QTimer::timeout, this, &DroneStatusWidget::onStatusUpdateTimer);
    m_statusUpdateTimer->start();
    
    // Simulation timer for demo data
    m_simulationTimer = new QTimer(this);
    m_simulationTimer->setInterval(500); // Update every 500ms
    connect(m_simulationTimer, &QTimer::timeout, [this]() {
        if (m_simulationMode) {
            // Simulate changing battery level
            m_simBatteryLevel -= 0.01f; // Drain 0.01% every 500ms
            if (m_simBatteryLevel < 0) m_simBatteryLevel = 100.0f;
            
            // Create simulated status
            DroneStatus simStatus;
            simStatus.connected = true;
            simStatus.batteryPercentage = m_simBatteryLevel;
            simStatus.batteryVoltage = 11.1f + (m_simBatteryLevel / 100.0f) * 1.5f;
            simStatus.flightMode = m_simFlightMode;
            simStatus.armed = m_simArmed;
            simStatus.gpsLock = true;
            simStatus.gpsNumSats = 12;
            simStatus.altitude = 10.5f + sin(QDateTime::currentMSecsSinceEpoch() / 1000.0) * 2.0f;
            simStatus.groundSpeed = m_simArmed ? 2.5f : 0.0f;
            simStatus.verticalSpeed = sin(QDateTime::currentMSecsSinceEpoch() / 2000.0) * 0.5f;
            simStatus.position = QVector3D(37.7749f, -122.4194f, simStatus.altitude);
            simStatus.velocity = QVector3D(simStatus.groundSpeed, 0, simStatus.verticalSpeed);
            simStatus.attitude = QVector3D(
                sin(QDateTime::currentMSecsSinceEpoch() / 3000.0) * 5.0f, // roll
                cos(QDateTime::currentMSecsSinceEpoch() / 4000.0) * 3.0f, // pitch
                45.0f // yaw
            );
            simStatus.lastHeartbeat = QDateTime::currentDateTime().toString("hh:mm:ss");
            simStatus.systemStatus = m_simArmed ? "ACTIVE" : "STANDBY";
            
            updateDroneStatus(simStatus);
        }
    });
    m_simulationTimer->start();
    
    // Add initial message
    addMessage("Drone Status Widget initialized", "info");
    addMessage("Running in simulation mode - Connect to real drone to see live data", "warning");
}

DroneStatusWidget::~DroneStatusWidget()
{
    delete ui;
}

void DroneStatusWidget::setupConnections()
{
    // Connect flight mode combo
    connect(ui->flightModeCombo, &QComboBox::currentTextChanged, this, &DroneStatusWidget::onFlightModeChanged);
    
    // Connect control buttons
    connect(ui->armDisarmButton, &QPushButton::clicked, this, &DroneStatusWidget::onArmDisarmClicked);
    connect(ui->takeoffButton, &QPushButton::clicked, this, &DroneStatusWidget::onTakeoffClicked);
    connect(ui->landButton, &QPushButton::clicked, this, &DroneStatusWidget::onLandClicked);
    connect(ui->rtlButton, &QPushButton::clicked, this, &DroneStatusWidget::onRTLClicked);
    connect(ui->emergencyStopButton, &QPushButton::clicked, this, &DroneStatusWidget::onEmergencyStopClicked);
    
    // Connect clear messages button
    connect(ui->clearMessagesButton, &QPushButton::clicked, this, &DroneStatusWidget::onClearMessages);
}

void DroneStatusWidget::updateDroneStatus(const DroneStatus &status)
{
    m_currentStatus = status;
    
    updateBatteryDisplay();
    updateFlightDisplay();
    updatePositionDisplay();
    updateControlsDisplay();
}

void DroneStatusWidget::setConnectionStatus(bool connected)
{
    m_currentStatus.connected = connected;
    updateFlightDisplay();
    updateControlsDisplay();
    
    if (connected) {
        addMessage("Connected to drone", "info");
        m_simulationMode = false;
    } else {
        addMessage("Disconnected from drone", "warning");
        m_simulationMode = true;
    }
}

void DroneStatusWidget::updateBatteryDisplay()
{
    float percentage = m_currentStatus.batteryPercentage;
    
    ui->batteryPercentageLabel->setText(QString("Battery: %1%").arg(percentage, 0, 'f', 1));
    ui->batteryProgressBar->setValue(static_cast<int>(percentage));
    ui->batteryVoltageLabel->setText(QString("Voltage: %1V").arg(m_currentStatus.batteryVoltage, 0, 'f', 2));
    
    // Update battery status color based on level
    QString statusText;
    QString color;
    if (percentage > 50) {
        statusText = "Good";
        color = "#10b981";
        ui->batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #10b981; border-radius: 3px; }"
        );
    } else if (percentage > 25) {
        statusText = "Warning";
        color = "#f59e0b";
        ui->batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #f59e0b; border-radius: 3px; }"
        );
    } else {
        statusText = "Critical";
        color = "#ef4444";
        ui->batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #ef4444; border-radius: 3px; }"
        );
    }
    
    ui->batteryStatusLabel->setText(QString("Status: %1").arg(statusText));
    ui->batteryStatusLabel->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; }").arg(color));
}

void DroneStatusWidget::updateFlightDisplay()
{
    // Connection status
    if (m_currentStatus.connected) {
        ui->connectionStatusLabel->setText("Connected");
        ui->connectionStatusLabel->setStyleSheet("QLabel { color: #10b981; font-weight: bold; }");
    } else {
        ui->connectionStatusLabel->setText("Disconnected");
        ui->connectionStatusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; }");
    }
    
    // Flight mode
    ui->flightModeLabel->setText(m_currentStatus.flightMode);
    
    // Armed status
    if (m_currentStatus.armed) {
        ui->armedStatusLabel->setText("Armed");
        ui->armedStatusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; }");
    } else {
        ui->armedStatusLabel->setText("Disarmed");
        ui->armedStatusLabel->setStyleSheet("QLabel { color: #10b981; }");
    }
    
    // GPS status
    if (m_currentStatus.gpsLock) {
        ui->gpsStatusLabel->setText(QString("3D Lock (%1 sats)").arg(m_currentStatus.gpsNumSats));
        ui->gpsStatusLabel->setStyleSheet("QLabel { color: #10b981; }");
    } else {
        ui->gpsStatusLabel->setText(QString("No Lock (%1 sats)").arg(m_currentStatus.gpsNumSats));
        ui->gpsStatusLabel->setStyleSheet("QLabel { color: #ef4444; }");
    }
    
    // Flight data
    ui->altitudeLabel->setText(QString("%1 m").arg(m_currentStatus.altitude, 0, 'f', 1));
    ui->groundSpeedLabel->setText(QString("%1 m/s").arg(m_currentStatus.groundSpeed, 0, 'f', 1));
    ui->verticalSpeedLabel->setText(QString("%1 m/s").arg(m_currentStatus.verticalSpeed, 0, 'f', 1));
    
    // System status
    ui->systemStatusLabel->setText(m_currentStatus.systemStatus);
    if (m_currentStatus.systemStatus == "ACTIVE") {
        ui->systemStatusLabel->setStyleSheet("QLabel { color: #10b981; font-weight: bold; }");
    } else if (m_currentStatus.systemStatus == "STANDBY") {
        ui->systemStatusLabel->setStyleSheet("QLabel { color: #f59e0b; font-weight: bold; }");
    } else {
        ui->systemStatusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; }");
    }
}

void DroneStatusWidget::updatePositionDisplay()
{
    ui->latitudeLabel->setText(formatCoordinate(m_currentStatus.position.x(), "°"));
    ui->longitudeLabel->setText(formatCoordinate(m_currentStatus.position.y(), "°"));
    ui->altitudeAbsLabel->setText(QString("%1 m").arg(m_currentStatus.position.z(), 0, 'f', 1));
    
    ui->rollLabel->setText(formatCoordinate(m_currentStatus.attitude.x(), "°"));
    ui->pitchLabel->setText(formatCoordinate(m_currentStatus.attitude.y(), "°"));
    ui->yawLabel->setText(formatCoordinate(m_currentStatus.attitude.z(), "°"));
}

void DroneStatusWidget::updateControlsDisplay()
{
    bool connected = m_currentStatus.connected;
    bool armed = m_currentStatus.armed;
    
    // Update flight mode combo
    ui->flightModeCombo->setEnabled(connected && !armed);
    
    // Update arm/disarm button
    if (armed) {
        ui->armDisarmButton->setText("DISARM");
        ui->armDisarmButton->setStyleSheet(
            "QPushButton { background-color: #10b981; color: white; border: none; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
            "QPushButton:hover { background-color: #047857; } "
            "QPushButton:disabled { background-color: #374151; }"
        );
    } else {
        ui->armDisarmButton->setText("ARM");
        ui->armDisarmButton->setStyleSheet(
            "QPushButton { background-color: #dc2626; color: white; border: none; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
            "QPushButton:hover { background-color: #b91c1c; } "
            "QPushButton:disabled { background-color: #374151; }"
        );
    }
    ui->armDisarmButton->setEnabled(connected);
    
    // Update other buttons
    ui->takeoffButton->setEnabled(connected && armed && m_currentStatus.gpsLock);
    ui->landButton->setEnabled(connected && armed);
    ui->rtlButton->setEnabled(connected && armed);
    ui->emergencyStopButton->setEnabled(connected);
}

void DroneStatusWidget::addMessage(const QString &message, const QString &type)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString color;
    QString icon;
    
    if (type == "error") {
        color = "#ef4444";
        icon = "❌";
    } else if (type == "warning") {
        color = "#f59e0b";
        icon = "⚠️";
    } else if (type == "info") {
        color = "#3b82f6";
        icon = "ℹ️";
    } else {
        color = "#9ca3af";
        icon = "📝";
    }
    
    QString formattedMessage = QString("[%1] %2 %3").arg(timestamp, icon, message);
    
    QListWidgetItem *item = new QListWidgetItem(formattedMessage);
    item->setForeground(QColor(color));
    
    ui->messagesList->insertItem(0, item); // Add to top
    
    // Limit message history to 100 items
    while (ui->messagesList->count() > 100) {
        delete ui->messagesList->takeItem(ui->messagesList->count() - 1);
    }
}

QString DroneStatusWidget::formatCoordinate(float value, const QString &unit)
{
    return QString("%1%2").arg(value, 0, 'f', 6).arg(unit);
}

void DroneStatusWidget::onArmDisarmClicked()
{
    bool shouldArm = !m_currentStatus.armed;
    
    if (shouldArm) {
        int ret = QMessageBox::question(this, "Arm Drone", 
                                       "Are you sure you want to ARM the drone?\n\nMake sure the area is clear and you are ready for flight.",
                                       QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
    }
    
    emit armDisarmRequested(shouldArm);
    
    // In simulation mode, update immediately
    if (m_simulationMode) {
        m_simArmed = shouldArm;
        addMessage(shouldArm ? "Drone armed" : "Drone disarmed", "info");
    }
}

void DroneStatusWidget::onTakeoffClicked()
{
    int ret = QMessageBox::question(this, "Takeoff", 
                                   "Initiate automatic takeoff?\n\nThe drone will take off to a safe altitude.",
                                   QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        emit takeoffRequested();
        addMessage("Takeoff initiated", "info");
    }
}

void DroneStatusWidget::onLandClicked()
{
    int ret = QMessageBox::question(this, "Land", 
                                   "Initiate automatic landing?\n\nThe drone will land at its current position.",
                                   QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        emit landRequested();
        addMessage("Landing initiated", "info");
    }
}

void DroneStatusWidget::onRTLClicked()
{
    int ret = QMessageBox::question(this, "Return to Launch", 
                                   "Return to launch position?\n\nThe drone will fly back to its takeoff location and land.",
                                   QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        emit returnToLaunchRequested();
        addMessage("Return to launch initiated", "info");
    }
}

void DroneStatusWidget::onEmergencyStopClicked()
{
    int ret = QMessageBox::critical(this, "Emergency Stop", 
                                   "🚨 EMERGENCY STOP 🚨\n\nThis will immediately stop all motors!\nThe drone will fall from the sky!\n\nOnly use in extreme emergencies!",
                                   QMessageBox::Yes | QMessageBox::Cancel);
    if (ret == QMessageBox::Yes) {
        emit emergencyStopRequested();
        addMessage("EMERGENCY STOP ACTIVATED", "error");
    }
}

void DroneStatusWidget::onFlightModeChanged(const QString &mode)
{
    if (m_currentStatus.connected) {
        emit flightModeChangeRequested(mode);
        addMessage(QString("Flight mode change requested: %1").arg(mode), "info");
        
        // In simulation mode, update immediately
        if (m_simulationMode) {
            m_simFlightMode = mode;
        }
    }
}

void DroneStatusWidget::onStatusUpdateTimer()
{
    // This would normally request fresh status from the drone controller
    // For now, it just updates the last heartbeat time
    if (m_currentStatus.connected) {
        m_currentStatus.lastHeartbeat = QDateTime::currentDateTime().toString("hh:mm:ss");
    }
}

void DroneStatusWidget::onClearMessages()
{
    ui->messagesList->clear();
    addMessage("Messages cleared", "info");
}