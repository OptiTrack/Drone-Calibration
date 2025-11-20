#include "dronestatuswidget.h"
#include <QDateTime>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QtMath>

DroneStatusWidget::DroneStatusWidget(QWidget *parent)
    : QWidget(parent)
    , ui(nullptr)
    , m_mainLayout(nullptr)
    , m_topLayout(nullptr)
    , m_leftLayout(nullptr)
    , m_rightLayout(nullptr)
    , m_batteryGroup(nullptr)
    , m_batteryLayout(nullptr)
    , m_batteryPercentageLabel(nullptr)
    , m_batteryProgressBar(nullptr)
    , m_batteryVoltageLabel(nullptr)
    , m_batteryStatusLabel(nullptr)
    , m_flightGroup(nullptr)
    , m_flightLayout(nullptr)
    , m_connectionStatusLabel(nullptr)
    , m_flightModeLabel(nullptr)
    , m_armedStatusLabel(nullptr)
    , m_gpsStatusLabel(nullptr)
    , m_altitudeLabel(nullptr)
    , m_groundSpeedLabel(nullptr)
    , m_verticalSpeedLabel(nullptr)
    , m_systemStatusLabel(nullptr)
    , m_positionGroup(nullptr)
    , m_positionLayout(nullptr)
    , m_latitudeLabel(nullptr)
    , m_longitudeLabel(nullptr)
    , m_altitudeAbsLabel(nullptr)
    , m_rollLabel(nullptr)
    , m_pitchLabel(nullptr)
    , m_yawLabel(nullptr)
    , m_controlsGroup(nullptr)
    , m_controlsLayout(nullptr)
    , m_flightModeCombo(nullptr)
    , m_armDisarmButton(nullptr)
    , m_takeoffButton(nullptr)
    , m_landButton(nullptr)
    , m_rtlButton(nullptr)
    , m_emergencyStopButton(nullptr)
    , m_messagesGroup(nullptr)
    , m_messagesLayout(nullptr)
    , m_messagesList(nullptr)
    , m_clearMessagesButton(nullptr)
    , m_statusUpdateTimer(nullptr)
    , m_simulationTimer(nullptr)
    , m_simulationMode(true)
    , m_simBatteryLevel(85.0f)
    , m_simArmed(false)
    , m_simFlightMode("STABILIZE")
{
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
    
    setupUI();
    
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
}

void DroneStatusWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);
    
    // Create top layout for main content
    m_topLayout = new QHBoxLayout;
    m_mainLayout->addLayout(m_topLayout);
    
    // Create left and right columns
    m_leftLayout = new QVBoxLayout;
    m_rightLayout = new QVBoxLayout;
    m_topLayout->addLayout(m_leftLayout, 1);
    m_topLayout->addLayout(m_rightLayout, 1);
    
    setupBatteryGroup();
    setupFlightGroup();
    setupPositionGroup();
    setupControlsGroup();
    setupMessagesGroup();
    
    // Add groups to layouts
    m_leftLayout->addWidget(m_batteryGroup);
    m_leftLayout->addWidget(m_flightGroup);
    m_leftLayout->addWidget(m_positionGroup);
    m_leftLayout->addStretch();
    
    m_rightLayout->addWidget(m_controlsGroup);
    m_rightLayout->addStretch();
    
    // Messages at the bottom
    m_mainLayout->addWidget(m_messagesGroup);
}

void DroneStatusWidget::setupBatteryGroup()
{
    m_batteryGroup = new QGroupBox("Battery Status");
    m_batteryGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    
    m_batteryLayout = new QVBoxLayout(m_batteryGroup);
    
    // Battery percentage
    m_batteryPercentageLabel = new QLabel("Battery: 0%");
    m_batteryPercentageLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: white; }");
    m_batteryLayout->addWidget(m_batteryPercentageLabel);
    
    // Battery progress bar
    m_batteryProgressBar = new QProgressBar;
    m_batteryProgressBar->setRange(0, 100);
    m_batteryProgressBar->setStyleSheet(
        "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
        "QProgressBar::chunk { background-color: #10b981; border-radius: 3px; }"
    );
    m_batteryLayout->addWidget(m_batteryProgressBar);
    
    // Battery voltage
    m_batteryVoltageLabel = new QLabel("Voltage: 0.0V");
    m_batteryVoltageLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_batteryLayout->addWidget(m_batteryVoltageLabel);
    
    // Battery status
    m_batteryStatusLabel = new QLabel("Status: Unknown");
    m_batteryStatusLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_batteryLayout->addWidget(m_batteryStatusLabel);
}

void DroneStatusWidget::setupFlightGroup()
{
    m_flightGroup = new QGroupBox("Flight Status");
    m_flightGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    
    m_flightLayout = new QGridLayout(m_flightGroup);
    
    // Connection status
    m_flightLayout->addWidget(new QLabel("Connection:"), 0, 0);
    m_connectionStatusLabel = new QLabel("Disconnected");
    m_connectionStatusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; }");
    m_flightLayout->addWidget(m_connectionStatusLabel, 0, 1);
    
    // Flight mode
    m_flightLayout->addWidget(new QLabel("Flight Mode:"), 1, 0);
    m_flightModeLabel = new QLabel("UNKNOWN");
    m_flightModeLabel->setStyleSheet("QLabel { color: #3b82f6; font-weight: bold; }");
    m_flightLayout->addWidget(m_flightModeLabel, 1, 1);
    
    // Armed status
    m_flightLayout->addWidget(new QLabel("Armed:"), 2, 0);
    m_armedStatusLabel = new QLabel("Disarmed");
    m_armedStatusLabel->setStyleSheet("QLabel { color: #10b981; }");
    m_flightLayout->addWidget(m_armedStatusLabel, 2, 1);
    
    // GPS status
    m_flightLayout->addWidget(new QLabel("GPS:"), 3, 0);
    m_gpsStatusLabel = new QLabel("No Lock (0 sats)");
    m_gpsStatusLabel->setStyleSheet("QLabel { color: #ef4444; }");
    m_flightLayout->addWidget(m_gpsStatusLabel, 3, 1);
    
    // Altitude
    m_flightLayout->addWidget(new QLabel("Altitude:"), 4, 0);
    m_altitudeLabel = new QLabel("0.0 m");
    m_altitudeLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_flightLayout->addWidget(m_altitudeLabel, 4, 1);
    
    // Ground speed
    m_flightLayout->addWidget(new QLabel("Ground Speed:"), 5, 0);
    m_groundSpeedLabel = new QLabel("0.0 m/s");
    m_groundSpeedLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_flightLayout->addWidget(m_groundSpeedLabel, 5, 1);
    
    // Vertical speed
    m_flightLayout->addWidget(new QLabel("Vertical Speed:"), 6, 0);
    m_verticalSpeedLabel = new QLabel("0.0 m/s");
    m_verticalSpeedLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_flightLayout->addWidget(m_verticalSpeedLabel, 6, 1);
    
    // System status
    m_flightLayout->addWidget(new QLabel("System Status:"), 7, 0);
    m_systemStatusLabel = new QLabel("STANDBY");
    m_systemStatusLabel->setStyleSheet("QLabel { color: #f59e0b; font-weight: bold; }");
    m_flightLayout->addWidget(m_systemStatusLabel, 7, 1);
}

void DroneStatusWidget::setupPositionGroup()
{
    m_positionGroup = new QGroupBox("Position & Attitude");
    m_positionGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    
    m_positionLayout = new QGridLayout(m_positionGroup);
    
    // Position
    m_positionLayout->addWidget(new QLabel("Latitude:"), 0, 0);
    m_latitudeLabel = new QLabel("0.000000°");
    m_latitudeLabel->setStyleSheet("QLabel { color: #9ca3af; font-family: monospace; }");
    m_positionLayout->addWidget(m_latitudeLabel, 0, 1);
    
    m_positionLayout->addWidget(new QLabel("Longitude:"), 1, 0);
    m_longitudeLabel = new QLabel("0.000000°");
    m_longitudeLabel->setStyleSheet("QLabel { color: #9ca3af; font-family: monospace; }");
    m_positionLayout->addWidget(m_longitudeLabel, 1, 1);
    
    m_positionLayout->addWidget(new QLabel("Altitude (ABS):"), 2, 0);
    m_altitudeAbsLabel = new QLabel("0.0 m");
    m_altitudeAbsLabel->setStyleSheet("QLabel { color: #9ca3af; font-family: monospace; }");
    m_positionLayout->addWidget(m_altitudeAbsLabel, 2, 1);
    
    // Attitude
    m_positionLayout->addWidget(new QLabel("Roll:"), 3, 0);
    m_rollLabel = new QLabel("0.0°");
    m_rollLabel->setStyleSheet("QLabel { color: #9ca3af; font-family: monospace; }");
    m_positionLayout->addWidget(m_rollLabel, 3, 1);
    
    m_positionLayout->addWidget(new QLabel("Pitch:"), 4, 0);
    m_pitchLabel = new QLabel("0.0°");
    m_pitchLabel->setStyleSheet("QLabel { color: #9ca3af; font-family: monospace; }");
    m_positionLayout->addWidget(m_pitchLabel, 4, 1);
    
    m_positionLayout->addWidget(new QLabel("Yaw:"), 5, 0);
    m_yawLabel = new QLabel("0.0°");
    m_yawLabel->setStyleSheet("QLabel { color: #9ca3af; font-family: monospace; }");
    m_positionLayout->addWidget(m_yawLabel, 5, 1);
}

void DroneStatusWidget::setupControlsGroup()
{
    m_controlsGroup = new QGroupBox("Flight Controls");
    m_controlsGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    
    m_controlsLayout = new QVBoxLayout(m_controlsGroup);
    
    // Flight mode selector
    m_controlsLayout->addWidget(new QLabel("Flight Mode:"));
    m_flightModeCombo = new QComboBox;
    m_flightModeCombo->addItems({"STABILIZE", "ALT_HOLD", "LOITER", "AUTO", "RTL", "LAND", "GUIDED"});
    m_flightModeCombo->setStyleSheet(
        "QComboBox { background-color: #374151; color: white; border: 1px solid #4b5563; padding: 4px; border-radius: 4px; } "
        "QComboBox::drop-down { border: none; } "
        "QComboBox QAbstractItemView { background-color: #374151; color: white; selection-background-color: #3b82f6; }"
    );
    m_controlsLayout->addWidget(m_flightModeCombo);
    
    m_controlsLayout->addWidget(new QLabel("")); // Spacer
    
    // Arm/Disarm button
    m_armDisarmButton = new QPushButton("ARM");
    m_armDisarmButton->setStyleSheet(
        "QPushButton { background-color: #dc2626; color: white; border: none; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #b91c1c; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    m_controlsLayout->addWidget(m_armDisarmButton);
    
    // Takeoff button
    m_takeoffButton = new QPushButton("TAKEOFF");
    m_takeoffButton->setStyleSheet(
        "QPushButton { background-color: #059669; color: white; border: none; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #047857; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    m_takeoffButton->setEnabled(false);
    m_controlsLayout->addWidget(m_takeoffButton);
    
    // Land button
    m_landButton = new QPushButton("LAND");
    m_landButton->setStyleSheet(
        "QPushButton { background-color: #f59e0b; color: white; border: none; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #d97706; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    m_landButton->setEnabled(false);
    m_controlsLayout->addWidget(m_landButton);
    
    // RTL button
    m_rtlButton = new QPushButton("RETURN TO LAUNCH");
    m_rtlButton->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; border: none; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #2563eb; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    m_rtlButton->setEnabled(false);
    m_controlsLayout->addWidget(m_rtlButton);
    
    m_controlsLayout->addWidget(new QLabel("")); // Spacer
    
    // Emergency stop button
    m_emergencyStopButton = new QPushButton("🚨 EMERGENCY STOP");
    m_emergencyStopButton->setStyleSheet(
        "QPushButton { background-color: #7c2d12; color: white; border: 2px solid #dc2626; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #92400e; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    m_controlsLayout->addWidget(m_emergencyStopButton);
    
    // Connect signals
    connect(m_flightModeCombo, &QComboBox::currentTextChanged, this, &DroneStatusWidget::onFlightModeChanged);
    connect(m_armDisarmButton, &QPushButton::clicked, this, &DroneStatusWidget::onArmDisarmClicked);
    connect(m_takeoffButton, &QPushButton::clicked, this, &DroneStatusWidget::onTakeoffClicked);
    connect(m_landButton, &QPushButton::clicked, this, &DroneStatusWidget::onLandClicked);
    connect(m_rtlButton, &QPushButton::clicked, this, &DroneStatusWidget::onRTLClicked);
    connect(m_emergencyStopButton, &QPushButton::clicked, this, &DroneStatusWidget::onEmergencyStopClicked);
}

void DroneStatusWidget::setupMessagesGroup()
{
    m_messagesGroup = new QGroupBox("System Messages");
    m_messagesGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    m_messagesGroup->setMaximumHeight(200);
    
    m_messagesLayout = new QVBoxLayout(m_messagesGroup);
    
    // Messages list
    m_messagesList = new QListWidget;
    m_messagesList->setStyleSheet(
        "QListWidget { background-color: #1f2937; color: white; border: 1px solid #4b5563; } "
        "QListWidget::item { padding: 4px; border-bottom: 1px solid #374151; } "
        "QListWidget::item:hover { background-color: #374151; }"
    );
    m_messagesLayout->addWidget(m_messagesList);
    
    // Clear button
    m_clearMessagesButton = new QPushButton("Clear Messages");
    m_clearMessagesButton->setStyleSheet(
        "QPushButton { background-color: #374151; color: white; border: 1px solid #4b5563; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #4b5563; }"
    );
    m_messagesLayout->addWidget(m_clearMessagesButton);
    
    connect(m_clearMessagesButton, &QPushButton::clicked, this, &DroneStatusWidget::onClearMessages);
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
    
    m_batteryPercentageLabel->setText(QString("Battery: %1%").arg(percentage, 0, 'f', 1));
    m_batteryProgressBar->setValue(static_cast<int>(percentage));
    m_batteryVoltageLabel->setText(QString("Voltage: %1V").arg(m_currentStatus.batteryVoltage, 0, 'f', 2));
    
    // Update battery status color based on level
    QString statusText;
    QString color;
    if (percentage > 50) {
        statusText = "Good";
        color = "#10b981";
        m_batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #10b981; border-radius: 3px; }"
        );
    } else if (percentage > 25) {
        statusText = "Warning";
        color = "#f59e0b";
        m_batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #f59e0b; border-radius: 3px; }"
        );
    } else {
        statusText = "Critical";
        color = "#ef4444";
        m_batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #ef4444; border-radius: 3px; }"
        );
    }
    
    m_batteryStatusLabel->setText(QString("Status: %1").arg(statusText));
    m_batteryStatusLabel->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; }").arg(color));
}

void DroneStatusWidget::updateFlightDisplay()
{
    // Connection status
    if (m_currentStatus.connected) {
        m_connectionStatusLabel->setText("Connected");
        m_connectionStatusLabel->setStyleSheet("QLabel { color: #10b981; font-weight: bold; }");
    } else {
        m_connectionStatusLabel->setText("Disconnected");
        m_connectionStatusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; }");
    }
    
    // Flight mode
    m_flightModeLabel->setText(m_currentStatus.flightMode);
    
    // Armed status
    if (m_currentStatus.armed) {
        m_armedStatusLabel->setText("Armed");
        m_armedStatusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; }");
    } else {
        m_armedStatusLabel->setText("Disarmed");
        m_armedStatusLabel->setStyleSheet("QLabel { color: #10b981; }");
    }
    
    // GPS status
    if (m_currentStatus.gpsLock) {
        m_gpsStatusLabel->setText(QString("3D Lock (%1 sats)").arg(m_currentStatus.gpsNumSats));
        m_gpsStatusLabel->setStyleSheet("QLabel { color: #10b981; }");
    } else {
        m_gpsStatusLabel->setText(QString("No Lock (%1 sats)").arg(m_currentStatus.gpsNumSats));
        m_gpsStatusLabel->setStyleSheet("QLabel { color: #ef4444; }");
    }
    
    // Flight data
    m_altitudeLabel->setText(QString("%1 m").arg(m_currentStatus.altitude, 0, 'f', 1));
    m_groundSpeedLabel->setText(QString("%1 m/s").arg(m_currentStatus.groundSpeed, 0, 'f', 1));
    m_verticalSpeedLabel->setText(QString("%1 m/s").arg(m_currentStatus.verticalSpeed, 0, 'f', 1));
    
    // System status
    m_systemStatusLabel->setText(m_currentStatus.systemStatus);
    if (m_currentStatus.systemStatus == "ACTIVE") {
        m_systemStatusLabel->setStyleSheet("QLabel { color: #10b981; font-weight: bold; }");
    } else if (m_currentStatus.systemStatus == "STANDBY") {
        m_systemStatusLabel->setStyleSheet("QLabel { color: #f59e0b; font-weight: bold; }");
    } else {
        m_systemStatusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; }");
    }
}

void DroneStatusWidget::updatePositionDisplay()
{
    m_latitudeLabel->setText(formatCoordinate(m_currentStatus.position.x(), "°"));
    m_longitudeLabel->setText(formatCoordinate(m_currentStatus.position.y(), "°"));
    m_altitudeAbsLabel->setText(QString("%1 m").arg(m_currentStatus.position.z(), 0, 'f', 1));
    
    m_rollLabel->setText(formatCoordinate(m_currentStatus.attitude.x(), "°"));
    m_pitchLabel->setText(formatCoordinate(m_currentStatus.attitude.y(), "°"));
    m_yawLabel->setText(formatCoordinate(m_currentStatus.attitude.z(), "°"));
}

void DroneStatusWidget::updateControlsDisplay()
{
    bool connected = m_currentStatus.connected;
    bool armed = m_currentStatus.armed;
    
    // Update flight mode combo
    m_flightModeCombo->setEnabled(connected && !armed);
    
    // Update arm/disarm button
    if (armed) {
        m_armDisarmButton->setText("DISARM");
        m_armDisarmButton->setStyleSheet(
            "QPushButton { background-color: #10b981; color: white; border: none; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
            "QPushButton:hover { background-color: #047857; } "
            "QPushButton:disabled { background-color: #374151; }"
        );
    } else {
        m_armDisarmButton->setText("ARM");
        m_armDisarmButton->setStyleSheet(
            "QPushButton { background-color: #dc2626; color: white; border: none; padding: 8px 16px; border-radius: 4px; font-weight: bold; } "
            "QPushButton:hover { background-color: #b91c1c; } "
            "QPushButton:disabled { background-color: #374151; }"
        );
    }
    m_armDisarmButton->setEnabled(connected);
    
    // Update other buttons
    m_takeoffButton->setEnabled(connected && armed && m_currentStatus.gpsLock);
    m_landButton->setEnabled(connected && armed);
    m_rtlButton->setEnabled(connected && armed);
    m_emergencyStopButton->setEnabled(connected);
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
    
    m_messagesList->insertItem(0, item); // Add to top
    
    // Limit message history to 100 items
    while (m_messagesList->count() > 100) {
        delete m_messagesList->takeItem(m_messagesList->count() - 1);
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
    m_messagesList->clear();
    addMessage("Messages cleared", "info");
}