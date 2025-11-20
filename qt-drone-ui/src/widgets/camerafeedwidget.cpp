#include "camerafeedwidget.h"
#include <QApplication>
#include <QScreen>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QPixmap>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QPainter>
#include <QPen>

CameraFeedWidget::CameraFeedWidget(QWidget *parent)
    : QWidget(parent)
    , ui(nullptr)
    , m_mainLayout(nullptr)
    , m_controlsLayout(nullptr)
    , m_topControlsLayout(nullptr)
    , m_bottomControlsLayout(nullptr)
    , m_videoWidget(nullptr)
    , m_demoImageLabel(nullptr)
    , m_recordButton(nullptr)
    , m_sourceButton(nullptr)
    , m_fullscreenButton(nullptr)
    , m_settingsButton(nullptr)
    , m_settingsGroup(nullptr)
    , m_settingsLayout(nullptr)
    , m_qualityCombo(nullptr)
    , m_formatCombo(nullptr)
    , m_framerateSlider(nullptr)
    , m_framerateLabel(nullptr)
    , m_zoomSlider(nullptr)
    , m_zoomLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_recordingTimeLabel(nullptr)
    , m_connectionProgress(nullptr)
    , m_camera(nullptr)
    , m_mediaRecorder(nullptr)
    , m_captureSession(nullptr)
    , m_networkManager(nullptr)
    , m_currentReply(nullptr)
    , m_recordingTimer(nullptr)
    , m_isRecording(false)
    , m_isFullscreen(false)
    , m_showControls(true)
    , m_compactMode(false)
    , m_feedSource("demo") // Start with demo by default
    , m_recordingDuration(0)
    , m_recordingStartTime(0)
    , m_voxlHost("192.168.1.10") // Default VOXL IP
    , m_voxlPort(8080)
{
    // Initialize settings
    m_settings.quality = "high";
    m_settings.format = "mp4";
    m_settings.framerate = 30;
    m_settings.zoom = 1;
    
    setupUI();
    setupCamera();
    setupNetworking();
    connectSignals();
    initializeFeed();
}

CameraFeedWidget::~CameraFeedWidget()
{
    if (m_camera) {
        m_camera->stop();
    }
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

void CameraFeedWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);
    
    // Create video display area
    m_videoWidget = new QVideoWidget;
    m_videoWidget->setMinimumSize(640, 480);
    m_videoWidget->setStyleSheet("QVideoWidget { background-color: black; border: 2px solid #374151; }");
    
    // Create demo image label
    m_demoImageLabel = new QLabel;
    m_demoImageLabel->setMinimumSize(640, 480);
    m_demoImageLabel->setAlignment(Qt::AlignCenter);
    m_demoImageLabel->setStyleSheet("QLabel { background-color: black; border: 2px solid #374151; color: white; }");
    m_demoImageLabel->setText("Demo Image Loading...");
    
    // Add video widgets to main layout
    m_mainLayout->addWidget(m_videoWidget);
    m_mainLayout->addWidget(m_demoImageLabel);
    
    // Initially show demo image
    m_videoWidget->hide();
    
    // Create top controls layout
    m_topControlsLayout = new QHBoxLayout;
    
    // Feed source button
    m_sourceButton = new QPushButton("📡 Demo Feed");
    m_sourceButton->setToolTip("Toggle between Demo, Live Camera, and VOXL feed");
    m_sourceButton->setStyleSheet(
        "QPushButton { "
        "   background-color: #374151; "
        "   color: white; "
        "   border: 1px solid #4b5563; "
        "   padding: 8px 12px; "
        "   border-radius: 4px; "
        "} "
        "QPushButton:hover { background-color: #4b5563; }"
    );
    
    // Settings button
    m_settingsButton = new QPushButton("⚙️ Settings");
    m_settingsButton->setCheckable(true);
    m_settingsButton->setStyleSheet(
        "QPushButton { "
        "   background-color: #374151; "
        "   color: white; "
        "   border: 1px solid #4b5563; "
        "   padding: 8px 12px; "
        "   border-radius: 4px; "
        "} "
        "QPushButton:hover { background-color: #4b5563; } "
        "QPushButton:checked { background-color: #3b82f6; }"
    );
    
    // Connection status
    m_statusLabel = new QLabel("Status: Demo Mode");
    m_statusLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    
    m_topControlsLayout->addWidget(m_sourceButton);
    m_topControlsLayout->addWidget(m_settingsButton);
    m_topControlsLayout->addStretch();
    m_topControlsLayout->addWidget(m_statusLabel);
    
    m_mainLayout->addLayout(m_topControlsLayout);
    
    // Create settings panel
    setupSettingsPanel();
    
    // Create bottom controls layout
    m_bottomControlsLayout = new QHBoxLayout;
    
    // Record button
    m_recordButton = new QPushButton("🔴 Start Recording");
    m_recordButton->setStyleSheet(
        "QPushButton { "
        "   background-color: #dc2626; "
        "   color: white; "
        "   border: none; "
        "   padding: 10px 20px; "
        "   border-radius: 4px; "
        "   font-weight: bold; "
        "} "
        "QPushButton:hover { background-color: #b91c1c; }"
    );
    
    // Fullscreen button
    m_fullscreenButton = new QPushButton("⛶ Fullscreen");
    m_fullscreenButton->setStyleSheet(
        "QPushButton { "
        "   background-color: #374151; "
        "   color: white; "
        "   border: 1px solid #4b5563; "
        "   padding: 8px 12px; "
        "   border-radius: 4px; "
        "} "
        "QPushButton:hover { background-color: #4b5563; }"
    );
    
    // Recording time display
    m_recordingTimeLabel = new QLabel("00:00");
    m_recordingTimeLabel->setStyleSheet(
        "QLabel { "
        "   color: #dc2626; "
        "   font-family: monospace; "
        "   font-size: 16px; "
        "   font-weight: bold; "
        "}"
    );
    m_recordingTimeLabel->hide();
    
    m_bottomControlsLayout->addWidget(m_recordButton);
    m_bottomControlsLayout->addWidget(m_fullscreenButton);
    m_bottomControlsLayout->addStretch();
    m_bottomControlsLayout->addWidget(m_recordingTimeLabel);
    
    m_mainLayout->addLayout(m_bottomControlsLayout);
}

void CameraFeedWidget::setupSettingsPanel()
{
    m_settingsGroup = new QGroupBox("Camera Settings");
    m_settingsGroup->setStyleSheet(
        "QGroupBox { "
        "   color: white; "
        "   border: 1px solid #4b5563; "
        "   border-radius: 4px; "
        "   margin-top: 1ex; "
        "   padding-top: 10px; "
        "} "
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   left: 10px; "
        "   padding: 0 5px 0 5px; "
        "}"
    );
    m_settingsGroup->hide();
    
    m_settingsLayout = new QVBoxLayout(m_settingsGroup);
    
    // Quality setting
    QHBoxLayout *qualityLayout = new QHBoxLayout;
    qualityLayout->addWidget(new QLabel("Quality:"));
    m_qualityCombo = new QComboBox;
    m_qualityCombo->addItems({"low", "medium", "high", "ultra"});
    m_qualityCombo->setCurrentText(m_settings.quality);
    qualityLayout->addWidget(m_qualityCombo);
    qualityLayout->addStretch();
    m_settingsLayout->addLayout(qualityLayout);
    
    // Format setting
    QHBoxLayout *formatLayout = new QHBoxLayout;
    formatLayout->addWidget(new QLabel("Format:"));
    m_formatCombo = new QComboBox;
    m_formatCombo->addItems({"mp4", "avi", "mov"});
    m_formatCombo->setCurrentText(m_settings.format);
    formatLayout->addWidget(m_formatCombo);
    formatLayout->addStretch();
    m_settingsLayout->addLayout(formatLayout);
    
    // Framerate setting
    QHBoxLayout *framerateLayout = new QHBoxLayout;
    framerateLayout->addWidget(new QLabel("Framerate:"));
    m_framerateSlider = new QSlider(Qt::Horizontal);
    m_framerateSlider->setRange(15, 60);
    m_framerateSlider->setValue(m_settings.framerate);
    m_framerateLabel = new QLabel(QString::number(m_settings.framerate) + " fps");
    framerateLayout->addWidget(m_framerateSlider);
    framerateLayout->addWidget(m_framerateLabel);
    m_settingsLayout->addLayout(framerateLayout);
    
    // Zoom setting
    QHBoxLayout *zoomLayout = new QHBoxLayout;
    zoomLayout->addWidget(new QLabel("Zoom:"));
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(1, 10);
    m_zoomSlider->setValue(m_settings.zoom);
    m_zoomLabel = new QLabel(QString::number(m_settings.zoom) + "x");
    zoomLayout->addWidget(m_zoomSlider);
    zoomLayout->addWidget(m_zoomLabel);
    m_settingsLayout->addLayout(zoomLayout);
    
    m_mainLayout->insertWidget(2, m_settingsGroup);
}

void CameraFeedWidget::setupCamera()
{
    // Create recording timer
    m_recordingTimer = new QTimer(this);
    m_recordingTimer->setInterval(1000); // Update every second
}

void CameraFeedWidget::setupNetworking()
{
    m_networkManager = new QNetworkAccessManager(this);
}

void CameraFeedWidget::connectSignals()
{
    connect(m_recordButton, &QPushButton::clicked, this, &CameraFeedWidget::onToggleRecording);
    connect(m_sourceButton, &QPushButton::clicked, this, &CameraFeedWidget::onToggleFeedSource);
    connect(m_fullscreenButton, &QPushButton::clicked, this, &CameraFeedWidget::onToggleFullscreen);
    connect(m_settingsButton, &QPushButton::toggled, m_settingsGroup, &QGroupBox::setVisible);
    
    connect(m_qualityCombo, &QComboBox::currentTextChanged, this, &CameraFeedWidget::onQualityChanged);
    connect(m_formatCombo, &QComboBox::currentTextChanged, this, &CameraFeedWidget::onFormatChanged);
    connect(m_framerateSlider, &QSlider::valueChanged, this, &CameraFeedWidget::onFramerateChanged);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &CameraFeedWidget::onZoomChanged);
    
    connect(m_recordingTimer, &QTimer::timeout, this, &CameraFeedWidget::onRecordingTimer);
}

void CameraFeedWidget::initializeFeed()
{
    loadDemoImage();
}

void CameraFeedWidget::loadDemoImage()
{
    // Create a simple demo image
    QPixmap demoPixmap(640, 480);
    demoPixmap.fill(Qt::black);
    
    QPainter painter(&demoPixmap);
    painter.setPen(QPen(Qt::white, 2));
    painter.setFont(QFont("Arial", 24));
    painter.drawText(demoPixmap.rect(), Qt::AlignCenter, 
                    "DRONE CAMERA FEED\n\nDemo Mode\n\nClick 'Feed Source' to switch\nto Live Camera or VOXL");
    
    // Add some crosshairs for drone-like appearance
    painter.setPen(QPen(Qt::green, 2));
    painter.drawLine(320 - 50, 240, 320 + 50, 240);
    painter.drawLine(320, 240 - 50, 320, 240 + 50);
    painter.drawEllipse(320 - 25, 240 - 25, 50, 50);
    
    m_demoImageLabel->setPixmap(demoPixmap.scaled(m_demoImageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_statusLabel->setText("Status: Demo Mode");
}

void CameraFeedWidget::onToggleRecording()
{
    if (!m_isRecording) {
        // Start recording
        m_recordingStartTime = QDateTime::currentMSecsSinceEpoch();
        m_recordingDuration = 0;
        
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        m_currentRecordingPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) 
                               + QString("/drone_recording_%1.%2").arg(timestamp, m_settings.format);
        
        m_isRecording = true;
        m_recordButton->setText("⏹️ Stop Recording");
        m_recordButton->setStyleSheet(
            "QPushButton { "
            "   background-color: #7c2d12; "
            "   color: white; "
            "   border: none; "
            "   padding: 10px 20px; "
            "   border-radius: 4px; "
            "   font-weight: bold; "
            "} "
            "QPushButton:hover { background-color: #92400e; }"
        );
        
        m_recordingTimeLabel->show();
        m_recordingTimer->start();
        
        // TODO: Start actual recording based on feed source
        
    } else {
        // Stop recording
        m_isRecording = false;
        m_recordButton->setText("🔴 Start Recording");
        m_recordButton->setStyleSheet(
            "QPushButton { "
            "   background-color: #dc2626; "
            "   color: white; "
            "   border: none; "
            "   padding: 10px 20px; "
            "   border-radius: 4px; "
            "   font-weight: bold; "
            "} "
            "QPushButton:hover { background-color: #b91c1c; }"
        );
        
        m_recordingTimeLabel->hide();
        m_recordingTimer->stop();
        
        saveRecording();
    }
}

void CameraFeedWidget::onToggleFeedSource()
{
    if (m_feedSource == "demo") {
        m_feedSource = "live";
        m_sourceButton->setText("📹 Live Camera");
        m_statusLabel->setText("Status: Connecting to camera...");
        
        // Try to initialize camera
        if (!m_camera) {
            m_camera = new QCamera(this);
            m_mediaRecorder = new QMediaRecorder(this);
        }
        
        try {
            // In Qt 6, we need to use QMediaCaptureSession
            if (!m_captureSession) {
                m_captureSession = new QMediaCaptureSession(this);
                m_captureSession->setCamera(m_camera);
                m_captureSession->setVideoOutput(m_videoWidget);
                m_captureSession->setRecorder(m_mediaRecorder);
            }
            m_camera->start();
            m_videoWidget->show();
            m_demoImageLabel->hide();
            m_statusLabel->setText("Status: Live Camera Active");
        } catch (...) {
            // Camera failed, try VOXL
            onToggleFeedSource();
        }
        
    } else if (m_feedSource == "live") {
        m_feedSource = "voxl";
        m_sourceButton->setText("🚁 VOXL Feed");
        m_statusLabel->setText("Status: Connecting to VOXL...");
        
        if (m_camera) {
            m_camera->stop();
        }
        
        connectToVOXL();
        
    } else {
        m_feedSource = "demo";
        m_sourceButton->setText("📡 Demo Feed");
        
        if (m_camera) {
            m_camera->stop();
        }
        
        m_videoWidget->hide();
        m_demoImageLabel->show();
        loadDemoImage();
    }
}

void CameraFeedWidget::connectToVOXL()
{
    // Create connection progress indicator
    if (!m_connectionProgress) {
        m_connectionProgress = new QProgressBar;
        m_connectionProgress->setRange(0, 0); // Indeterminate progress
        m_bottomControlsLayout->insertWidget(2, m_connectionProgress);
    }
    m_connectionProgress->show();
    
    // Try to connect to VOXL camera stream
    QString url = QString("http://%1:%2/camera/stream").arg(m_voxlHost).arg(m_voxlPort);
    QNetworkRequest request(url);
    
    m_currentReply = m_networkManager->get(request);
    connect(m_currentReply, &QNetworkReply::finished, this, &CameraFeedWidget::onNetworkReplyFinished);
}

void CameraFeedWidget::onNetworkReplyFinished()
{
    if (m_connectionProgress) {
        m_connectionProgress->hide();
    }
    
    if (m_currentReply->error() == QNetworkReply::NoError) {
        m_statusLabel->setText("Status: VOXL Connected");
        // TODO: Process VOXL stream data
        m_videoWidget->show();
        m_demoImageLabel->hide();
    } else {
        m_statusLabel->setText("Status: VOXL Connection Failed - Using Demo");
        m_feedSource = "demo";
        m_sourceButton->setText("📡 Demo Feed");
        loadDemoImage();
    }
    
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void CameraFeedWidget::onToggleFullscreen()
{
    m_isFullscreen = !m_isFullscreen;
    
    if (m_isFullscreen) {
        setWindowState(Qt::WindowFullScreen);
        m_fullscreenButton->setText("⛷ Exit Fullscreen");
    } else {
        setWindowState(Qt::WindowNoState);
        m_fullscreenButton->setText("⛶ Fullscreen");
    }
}

void CameraFeedWidget::onZoomChanged(int value)
{
    m_settings.zoom = value;
    m_zoomLabel->setText(QString::number(value) + "x");
    
    // TODO: Apply zoom to active feed
}

void CameraFeedWidget::onQualityChanged(const QString &quality)
{
    m_settings.quality = quality;
}

void CameraFeedWidget::onFormatChanged(const QString &format)
{
    m_settings.format = format;
}

void CameraFeedWidget::onFramerateChanged(int framerate)
{
    m_settings.framerate = framerate;
    m_framerateLabel->setText(QString::number(framerate) + " fps");
}

void CameraFeedWidget::onRecordingTimer()
{
    m_recordingDuration++;
    int minutes = m_recordingDuration / 60;
    int seconds = m_recordingDuration % 60;
    m_recordingTimeLabel->setText(QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0')));
}

void CameraFeedWidget::saveRecording()
{
    // Create dummy recording data for now
    QByteArray recordingData;
    recordingData.append("Dummy recording data for ");
    recordingData.append(m_currentRecordingPath.toUtf8());
    
    emit recordingSaved(m_currentRecordingPath, recordingData);
}

void CameraFeedWidget::setCompactMode(bool compact)
{
    m_compactMode = compact;
    if (compact) {
        m_settingsGroup->hide();
        m_settingsButton->setChecked(false);
    }
}

void CameraFeedWidget::setShowControls(bool show)
{
    m_showControls = show;
    m_topControlsLayout->parentWidget()->setVisible(show);
    m_bottomControlsLayout->parentWidget()->setVisible(show);
}

void CameraFeedWidget::onCameraError()
{
    QMessageBox::warning(this, "Camera Error", "Failed to access camera. Switching to demo mode.");
    m_feedSource = "demo";
    onToggleFeedSource();
}