#include "dronestatuswidget.h"
#include "ui_dronestatuswidget.h"
#include <QDateTime>
#include <QMessageBox>
#include <QListWidgetItem>

DroneStatusWidget::DroneStatusWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DroneStatusWidget)
    , m_hideMapperMeshMessagesCheckBox(nullptr)
    , m_statusUpdateTimer(nullptr)
{
    ui->setupUi(this);
    
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
    
    setupConnections();
    
    // Set up timers
    m_statusUpdateTimer = new QTimer(this);
    m_statusUpdateTimer->setInterval(1000); // Update every second
    connect(m_statusUpdateTimer, &QTimer::timeout, this, &DroneStatusWidget::onStatusUpdateTimer);
    m_statusUpdateTimer->start();
    
    // Add initial message
    addMessage("Drone Status Widget initialized", "info");
    addMessage("Connect to a drone to see live telemetry", "info");
    updateDroneStatus(m_currentStatus);
}

DroneStatusWidget::~DroneStatusWidget()
{
    delete ui;
}

void DroneStatusWidget::setupConnections()
{
    ui->batteryStatusLabel->hide();
    ui->flightModeSelectLabel->hide();
    ui->flightModeCombo->hide();
    
    // Connect control buttons
    connect(ui->armDisarmButton, &QPushButton::clicked, this, &DroneStatusWidget::onArmDisarmClicked);
    connect(ui->takeoffButton, &QPushButton::clicked, this, &DroneStatusWidget::onTakeoffClicked);
    connect(ui->landButton, &QPushButton::clicked, this, &DroneStatusWidget::onLandClicked);
    connect(ui->rtlButton, &QPushButton::clicked, this, &DroneStatusWidget::onRTLClicked);
    connect(ui->forceDisarmButton, &QPushButton::clicked, this, &DroneStatusWidget::onForceDisarmClicked);
    connect(ui->flightTerminationButton, &QPushButton::clicked, this, &DroneStatusWidget::onFlightTerminationClicked);
    
    // Connect clear messages button
    connect(ui->clearMessagesButton, &QPushButton::clicked, this, &DroneStatusWidget::onClearMessages);

    m_hideMapperMeshMessagesCheckBox = new QCheckBox("Hide VOXL mesh update messages", this);
    m_hideMapperMeshMessagesCheckBox->setChecked(true);
    m_hideMapperMeshMessagesCheckBox->setStyleSheet("QCheckBox { color: #d1d5db; padding: 2px; }");
    ui->messagesLayout->insertWidget(0, m_hideMapperMeshMessagesCheckBox);
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
    
    if (connected) {
        addMessage("Connected to drone", "info");
    } else {
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
        addMessage("Disconnected from drone", "warning");
    }

    updateDroneStatus(m_currentStatus);
}

bool DroneStatusWidget::addSystemMessage(const QString &message, const QString &type)
{
    if (m_hideMapperMeshMessagesCheckBox && m_hideMapperMeshMessagesCheckBox->isChecked()) {
        if (message.startsWith(QStringLiteral("VOXL Mapper mesh update")) ||
            message.startsWith(QStringLiteral("VOXL Mapper mesh cleared"))) {
            return false;
        }
    }
    addMessage(message, type);
    return true;
}

void DroneStatusWidget::updateBatteryDisplay()
{
    if (!m_currentStatus.connected) {
        ui->batteryPercentageLabel->setText("Battery: --");
        ui->batteryProgressBar->setValue(0);
        ui->batteryVoltageLabel->setText("Voltage: --");
        ui->batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #4b5563; border-radius: 3px; }"
        );
        return;
    }

    float percentage = m_currentStatus.batteryPercentage;
    
    ui->batteryPercentageLabel->setText(QString("Battery: %1%").arg(percentage, 0, 'f', 1));
    ui->batteryProgressBar->setValue(static_cast<int>(percentage));
    ui->batteryVoltageLabel->setText(QString("Voltage: %1V").arg(m_currentStatus.batteryVoltage, 0, 'f', 2));
    
    // Update battery status color based on level
    if (percentage > 50) {
        ui->batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #10b981; border-radius: 3px; }"
        );
    } else if (percentage > 25) {
        ui->batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #f59e0b; border-radius: 3px; }"
        );
    } else {
        ui->batteryProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
            "QProgressBar::chunk { background-color: #ef4444; border-radius: 3px; }"
        );
    }
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
        ui->flightModeLabel->setText("--");
        ui->armedStatusLabel->setText("--");
        ui->armedStatusLabel->setStyleSheet("QLabel { color: #9ca3af; }");
        ui->gpsStatusLabel->setText("--");
        ui->gpsStatusLabel->setStyleSheet("QLabel { color: #9ca3af; }");
        ui->altitudeLabel->setText("--");
        ui->groundSpeedLabel->setText("--");
        ui->verticalSpeedLabel->setText("--");
        ui->systemStatusLabel->setText("DISCONNECTED");
        ui->systemStatusLabel->setStyleSheet("QLabel { color: #9ca3af; font-weight: bold; }");
        return;
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
    if (m_currentStatus.connected) {
        const QString degree = QString::fromUtf8("\xC2\xB0");
        if (m_currentStatus.positionIsMapperLocal) {
            ui->positionGroup->setTitle("VOXL Mapper Local Position & Attitude");
            ui->latitudeLabel->setText(QString("X: %1 m").arg(m_currentStatus.position.x(), 0, 'f', 3));
            ui->longitudeLabel->setText(QString("Y: %1 m").arg(m_currentStatus.position.y(), 0, 'f', 3));
            ui->altitudeAbsLabel->setText(QString("Z: %1 m").arg(m_currentStatus.position.z(), 0, 'f', 3));
        } else {
            ui->positionGroup->setTitle("GPS Position & Attitude");
            ui->latitudeLabel->setText(QString("Latitude: %1%2").arg(m_currentStatus.position.x(), 0, 'f', 6).arg(degree));
            ui->longitudeLabel->setText(QString("Longitude: %1%2").arg(m_currentStatus.position.y(), 0, 'f', 6).arg(degree));
            ui->altitudeAbsLabel->setText(QString("Alt: %1 m").arg(m_currentStatus.position.z(), 0, 'f', 1));
        }

        ui->rollLabel->setText(QString("Roll: %1%2").arg(m_currentStatus.attitude.x(), 0, 'f', 3).arg(degree));
        ui->pitchLabel->setText(QString("Pitch: %1%2").arg(m_currentStatus.attitude.y(), 0, 'f', 3).arg(degree));
        ui->yawLabel->setText(QString("Yaw: %1%2").arg(m_currentStatus.attitude.z(), 0, 'f', 3).arg(degree));
        return;
    }

    if (!m_currentStatus.connected) {
        ui->latitudeLabel->setText("--");
        ui->longitudeLabel->setText("--");
        ui->altitudeAbsLabel->setText("--");
        ui->rollLabel->setText("--");
        ui->pitchLabel->setText("--");
        ui->yawLabel->setText("--");
        ui->positionGroup->setTitle("Drone Pose");
        return;
    }

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
    ui->forceDisarmButton->setEnabled(connected);
    ui->flightTerminationButton->setEnabled(connected);
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

void DroneStatusWidget::onForceDisarmClicked()
{
    const int ret = QMessageBox::warning(
        this,
        QStringLiteral("Force disarm"),
        QStringLiteral("Send MAVLink force-disarm?\n\n"
                       "Motors stop immediately; the vehicle may fall. "
                       "This is a software command to PX4 (not a power cut). "
                       "You can usually re-arm after landing and checks."),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (ret == QMessageBox::Yes) {
        emit forceDisarmRequested();
        addMessage(QStringLiteral("Force disarm command sent"), "warning");
    }
}

void DroneStatusWidget::onFlightTerminationClicked()
{
    const int ret = QMessageBox::critical(
        this,
        QStringLiteral("Flight termination"),
        QStringLiteral("Send PX4 flight termination (MAV_CMD_DO_FLIGHTTERMINATION)?\n\n"
                       "This is the strongest software stop PX4 exposes: outputs go to configured "
                       "failsafe values and the flight may be ended until power-cycle, depending on "
                       "firmware and CBRK_FLIGHTTERM.\n\n"
                       "Does not replace a hardware estop or battery disconnect."),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (ret == QMessageBox::Yes) {
        emit flightTerminationRequested();
        addMessage(QStringLiteral("Flight termination command sent"), "error");
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
