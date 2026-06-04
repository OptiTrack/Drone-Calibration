#include "camerafeedwidget.h"
#include <QApplication>
#include <QScreen>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QPixmap>
#include <QImage>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFrame>
#include <QNetworkRequest>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <QUrl>
#include <QMediaFormat>
#include <QVideoFrame>
#include <algorithm>

namespace {
constexpr qsizetype MaxPortalStreamBufferBytes = 4 * 1024 * 1024;
}

CameraFeedWidget::CameraFeedWidget(QWidget *parent)
    : QWidget(parent)
    , ui(nullptr)
    , m_mainLayout(nullptr)
    , m_controlsLayout(nullptr)
    , m_topControlsLayout(nullptr)
    , m_bottomControlsLayout(nullptr)
    , m_cameraFeedLabel(nullptr)
    , m_cameraTitleLabel(nullptr)
    , m_cameraSelectCombo(nullptr)
    , m_recordButton(nullptr)
    , m_fullscreenButton(nullptr)
    , m_statusLabel(nullptr)
    , m_recordingTimeLabel(nullptr)
    , m_connectionProgress(nullptr)
    , m_networkManager(nullptr)
    , m_currentReply(nullptr)
    , m_currentCameraIndex(0)
    , m_captureSession(nullptr)
    , m_mediaRecorder(nullptr)
    , m_videoFrameInput(nullptr)
    , m_recordingTimer(nullptr)
    , m_isRecording(false)
    , m_isFullscreen(false)
    , m_showControls(true)
    , m_compactMode(false)
    , m_recordingDuration(0)
    , m_recordingStartTime(0)
    , m_voxlHost()
    , m_voxlPort(8900)
{
    setupUI();
    setupCamera();
    setupNetworking();
    connectSignals();
    initializeFeed();
}

CameraFeedWidget::~CameraFeedWidget()
{
    stopActiveFeeds();
    if (m_mediaRecorder && m_mediaRecorder->recorderState() == QMediaRecorder::RecordingState)
        m_mediaRecorder->stop();
}

void CameraFeedWidget::setupUI()
{
    setStyleSheet("QWidget { background-color: #111827; }");
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 8, 8, 8);
    m_mainLayout->setSpacing(6);

    // Camera panel
    QFrame *panel = new QFrame;
    panel->setFrameShape(QFrame::NoFrame);
    panel->setStyleSheet(
        "QFrame { background-color: #1f2937; border: 1px solid #374151; border-radius: 4px; }");
    QVBoxLayout *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(4, 4, 4, 4);
    panelLayout->setSpacing(4);

    m_cameraTitleLabel = new QLabel("No camera selected");
    m_cameraTitleLabel->setAlignment(Qt::AlignCenter);
    m_cameraTitleLabel->setStyleSheet(
        "QLabel { color: #9ca3af; font-size: 11px; font-weight: bold;"
        "         background: transparent; border: none; }");
    panelLayout->addWidget(m_cameraTitleLabel);

    m_cameraFeedLabel = new QLabel;
    m_cameraFeedLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_cameraFeedLabel->setAlignment(Qt::AlignCenter);
    m_cameraFeedLabel->setStyleSheet(
        "QLabel { background-color: #000000; border: none; color: #6b7280; font-size: 13px; }");
    m_cameraFeedLabel->setText("Waiting for drone connection\u2026");
    panelLayout->addWidget(m_cameraFeedLabel, 1);

    m_mainLayout->addWidget(panel, 1);

    // Bottom controls
    m_bottomControlsLayout = new QHBoxLayout;
    m_bottomControlsLayout->setSpacing(8);

    m_recordButton = new QPushButton("\u25CF Record");
    m_recordButton->setStyleSheet(
        "QPushButton { background-color: #dc2626; color: white; border: none; padding: 8px 16px;"
        "              border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #b91c1c; }");

    QLabel *camLabel = new QLabel("Camera:");
    camLabel->setStyleSheet("QLabel { color: #9ca3af; font-size: 12px; }");

    m_cameraSelectCombo = new QComboBox;
    m_cameraSelectCombo->setMinimumWidth(200);
    m_cameraSelectCombo->setStyleSheet(
        "QComboBox { background-color: #374151; color: #d1d5db; border: 1px solid #4b5563;"
        "            border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QComboBox:hover { background-color: #4b5563; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #1f2937; color: #d1d5db;"
        "                              selection-background-color: #3b82f6; border: 1px solid #4b5563; }");
    m_cameraSelectCombo->addItem("Waiting for drone\u2026");
    m_cameraSelectCombo->setEnabled(false);

    m_recordingTimeLabel = new QLabel("00:00");
    m_recordingTimeLabel->setStyleSheet(
        "QLabel { color: #dc2626; font-family: monospace; font-size: 14px; font-weight: bold; }");
    m_recordingTimeLabel->hide();

    m_statusLabel = new QLabel("Status: Waiting for connection");
    m_statusLabel->setStyleSheet("QLabel { color: #6b7280; font-size: 12px; }");

    m_bottomControlsLayout->addWidget(m_recordButton);
    m_bottomControlsLayout->addWidget(m_recordingTimeLabel);
    m_bottomControlsLayout->addSpacing(12);
    m_bottomControlsLayout->addWidget(camLabel);
    m_bottomControlsLayout->addWidget(m_cameraSelectCombo);
    m_bottomControlsLayout->addStretch();
    m_bottomControlsLayout->addWidget(m_statusLabel);

    m_mainLayout->addLayout(m_bottomControlsLayout);
}



void CameraFeedWidget::setupCamera()
{
    m_recordingTimer = new QTimer(this);
    m_recordingTimer->setInterval(1000);

    m_videoFrameInput = new QVideoFrameInput(this);
    m_captureSession  = new QMediaCaptureSession(this);
    m_mediaRecorder   = new QMediaRecorder(this);

    m_captureSession->setVideoFrameInput(m_videoFrameInput);
    m_captureSession->setRecorder(m_mediaRecorder);

    QMediaFormat fmt;
    fmt.setFileFormat(QMediaFormat::MPEG4);
    fmt.setVideoCodec(QMediaFormat::VideoCodec::H264);
    m_mediaRecorder->setMediaFormat(fmt);
    m_mediaRecorder->setQuality(QMediaRecorder::HighQuality);

    connect(m_mediaRecorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState && !m_currentRecordingPath.isEmpty())
            emit recordingSaved(m_currentRecordingPath, QByteArray{});
    });
}

void CameraFeedWidget::setupNetworking()
{
    m_networkManager = new QNetworkAccessManager(this);
}

void CameraFeedWidget::connectSignals()
{
    connect(m_recordButton, &QPushButton::clicked, this, &CameraFeedWidget::onToggleRecording);
    connect(m_recordingTimer, &QTimer::timeout, this, &CameraFeedWidget::onRecordingTimer);
    connect(m_cameraSelectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraFeedWidget::onCameraSelected);
}

void CameraFeedWidget::initializeFeed()
{
    // Camera streams connect automatically when setVoxlHost() is called on drone connect.
}

void CameraFeedWidget::stopActiveFeeds()
{
    if (m_currentReply) {
        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    m_portalStreamBuffer.clear();
    if (m_connectionProgress)
        m_connectionProgress->hide();
}



void CameraFeedWidget::onToggleRecording()
{
    if (!m_isRecording) {
        m_recordingStartTime = QDateTime::currentMSecsSinceEpoch();
        m_recordingDuration = 0;

        const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        const QString moviesDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                                  + QStringLiteral("/CaliDrone");
        QDir().mkpath(moviesDir);
        const QString camName = m_availableCameras.value(m_currentCameraIndex, QStringLiteral("drone"));
        m_currentRecordingPath = moviesDir + QStringLiteral("/%1_%2.mp4").arg(camName, timestamp);

        m_mediaRecorder->setOutputLocation(QUrl::fromLocalFile(m_currentRecordingPath));
        m_mediaRecorder->record();

        if (m_mediaRecorder->error() != QMediaRecorder::NoError) {
            m_statusLabel->setText(QStringLiteral("Status: Cannot start recording — %1").arg(m_mediaRecorder->errorString()));
            return;
        }

        m_isRecording = true;
        m_recordButton->setText("\u25A0 Stop");
        m_recordButton->setStyleSheet(
            "QPushButton { background-color: #7c2d12; color: white; border: none; padding: 8px 16px;"
            "              border-radius: 4px; font-weight: bold; }"
            "QPushButton:hover { background-color: #92400e; }");
        m_recordingTimeLabel->show();
        m_recordingTimer->start();
    } else {
        m_isRecording = false;
        m_recordButton->setText("\u25CF Record");
        m_recordButton->setStyleSheet(
            "QPushButton { background-color: #dc2626; color: white; border: none; padding: 8px 16px;"
            "              border-radius: 4px; font-weight: bold; }"
            "QPushButton:hover { background-color: #b91c1c; }");
        m_recordingTimeLabel->hide();
        m_recordingTimer->stop();
        saveRecording();
    }
}



void CameraFeedWidget::connectToPortalCamera(const QUrl &url)
{
    if (!url.isValid()) {
        if (m_cameraFeedLabel)
            m_cameraFeedLabel->setText(QStringLiteral("Invalid camera URL"));
        return;
    }

    // Null FIRST so the finished lambda sees nullptr and returns early, preventing double-delete
    if (m_currentReply) {
        QNetworkReply *old = m_currentReply;
        m_currentReply = nullptr;
        old->abort();
        old->deleteLater();
    }
    m_portalStreamBuffer.clear();

    if (m_cameraFeedLabel)
        m_cameraFeedLabel->setText(QStringLiteral("Connecting\u2026"));
    m_statusLabel->setText(QStringLiteral("Status: Connecting\u2026"));

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "image/jpeg,multipart/x-mixed-replace,*/*");
    QNetworkReply *reply = m_networkManager->get(request);
    m_currentReply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (m_currentReply != reply) return;
        m_portalStreamBuffer.append(reply->readAll());
        processPortalCameraBytes();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_currentReply != reply) return;
        m_portalStreamBuffer.append(reply->readAll());
        processPortalCameraBytes();
        if (reply->error() != QNetworkReply::NoError
                && reply->error() != QNetworkReply::OperationCanceledError) {
            if (m_cameraFeedLabel)
                m_cameraFeedLabel->setText(QStringLiteral("Camera disconnected"));
            m_statusLabel->setText(
                QStringLiteral("Status: Camera failed \u2014 %1").arg(reply->errorString()));
        }
        m_currentReply = nullptr;
        reply->deleteLater();
    });
}

void CameraFeedWidget::processPortalCameraBytes()
{
    if (m_portalStreamBuffer.size() > MaxPortalStreamBufferBytes)
        m_portalStreamBuffer.remove(0, m_portalStreamBuffer.size() - MaxPortalStreamBufferBytes);

    const QByteArray startMarker = QByteArray::fromHex("ffd8");
    const QByteArray endMarker = QByteArray::fromHex("ffd9");

    int start = m_portalStreamBuffer.indexOf(startMarker);
    while (start >= 0) {
        const int end = m_portalStreamBuffer.indexOf(endMarker, start + startMarker.size());
        if (end < 0) {
            if (start > 0)
                m_portalStreamBuffer.remove(0, start);
            return;
        }

        const int frameEnd = end + endMarker.size();
        const QByteArray frame = m_portalStreamBuffer.mid(start, frameEnd - start);
        m_portalStreamBuffer.remove(0, frameEnd);

        QPixmap pixmap;
        if (pixmap.loadFromData(frame, "JPG")) {
            if (m_connectionProgress)
                m_connectionProgress->hide();
            if (m_cameraFeedLabel)
                m_cameraFeedLabel->setPixmap(
                    pixmap.scaled(m_cameraFeedLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            const QString camName = m_availableCameras.value(m_currentCameraIndex, QStringLiteral("camera"));
            m_statusLabel->setText(QStringLiteral("Status: %1 active").arg(camName));
            // Feed frame into the MP4 recorder if active
            if (m_isRecording) {
                QImage img = pixmap.toImage().convertToFormat(QImage::Format_RGBX8888);
                QVideoFrame vf(img);
                m_videoFrameInput->sendVideoFrame(vf);
            }
        }

        start = m_portalStreamBuffer.indexOf(startMarker);
    }
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

void CameraFeedWidget::onRecordingTimer()
{
    m_recordingDuration++;
    int minutes = m_recordingDuration / 60;
    int seconds = m_recordingDuration % 60;
    m_recordingTimeLabel->setText(QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0')));
}

void CameraFeedWidget::saveRecording()
{
    m_mediaRecorder->stop();
    // recordingSaved is emitted from the recorderStateChanged signal when stop completes
}

void CameraFeedWidget::setCompactMode(bool compact)
{
    m_compactMode = compact;
}

void CameraFeedWidget::setShowControls(bool show)
{
    m_showControls = show;
    if (m_recordButton) m_recordButton->setVisible(show);
    if (m_statusLabel) m_statusLabel->setVisible(show);
}

void CameraFeedWidget::setVoxlHost(const QString &host)
{
    const QString cleanHost = host.trimmed();
    if (cleanHost.isEmpty())
        return;

    m_voxlHost = cleanHost;
    stopActiveFeeds();
    m_availableCameras.clear();
    m_currentCameraIndex = 0;
    m_cameraSelectCombo->blockSignals(true);
    m_cameraSelectCombo->clear();
    m_cameraSelectCombo->addItem(QStringLiteral("Discovering cameras\u2026"));
    m_cameraSelectCombo->setEnabled(false);
    m_cameraSelectCombo->blockSignals(false);
    m_cameraTitleLabel->setText(QStringLiteral("Discovering cameras\u2026"));
    m_cameraFeedLabel->setText(QStringLiteral("Connecting\u2026"));
    m_statusLabel->setText(QStringLiteral("Status: Connecting to %1\u2026").arg(m_voxlHost));
    fetchCameraList();
}

void CameraFeedWidget::fetchCameraList()
{
    QNetworkRequest req(QUrl(QStringLiteral("http://%1/_cmd/list_cameras").arg(m_voxlHost)));
    QNetworkReply *reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Fallback to known defaults if the endpoint fails
            m_availableCameras = { QStringLiteral("hires_small_front"), QStringLiteral("hires_small_down") };
        } else {
            const QString raw = QString::fromUtf8(reply->readAll()).trimmed();
            const QStringList all = raw.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            QSet<QString> seen;
            for (const QString &cam : all) {
                if (cam.contains(QStringLiteral("snapshot"))) continue;
                if (cam.contains(QStringLiteral("encoded")))  continue;
                if (cam.contains(QStringLiteral("bayer")))    continue;
                if (cam.contains(QStringLiteral("_full")))    continue;
                if (cam.contains(QStringLiteral("large")))    continue;
                if (cam.startsWith(QStringLiteral("tof_")))   continue;
                if (cam.contains(QStringLiteral("_misp_")))   continue;
                if (seen.contains(cam))                       continue;
                seen.insert(cam);
                m_availableCameras.append(cam);
            }
            // Sort: front before down, overlays at the bottom
            std::stable_sort(m_availableCameras.begin(), m_availableCameras.end(),
                [](const QString &a, const QString &b) {
                    const bool aOverlay = a.contains(QStringLiteral("overlay"));
                    const bool bOverlay = b.contains(QStringLiteral("overlay"));
                    if (aOverlay != bOverlay) return !aOverlay; // overlays last
                    const bool aDown = a.contains(QStringLiteral("_down"));
                    const bool bDown = b.contains(QStringLiteral("_down"));
                    if (aDown != bDown) return !aDown; // front before down
                    return false; // preserve relative order otherwise
                });
        }
        if (m_availableCameras.isEmpty())
            m_availableCameras = { QStringLiteral("hires_small_front"), QStringLiteral("hires_small_down") };

        // Populate the combo box
        m_cameraSelectCombo->blockSignals(true);
        m_cameraSelectCombo->clear();
        for (const QString &cam : std::as_const(m_availableCameras))
            m_cameraSelectCombo->addItem(cam);
        m_cameraSelectCombo->setEnabled(true);
        m_cameraSelectCombo->blockSignals(false);

        m_currentCameraIndex = 0;
        m_cameraSelectCombo->setCurrentIndex(0);
        connectActiveCamera();
    });
}

void CameraFeedWidget::connectActiveCamera()
{
    if (m_voxlHost.isEmpty() || m_availableCameras.isEmpty()) return;
    const QString &cam = m_availableCameras.at(m_currentCameraIndex);
    m_cameraTitleLabel->setText(cam);
    connectToPortalCamera(QUrl(QStringLiteral("http://%1/video_raw/%2").arg(m_voxlHost, cam)));
}

void CameraFeedWidget::onCameraSelected(int index)
{
    if (index < 0 || index >= m_availableCameras.size()) return;
    m_currentCameraIndex = index;
    if (m_cameraFeedLabel)
        m_cameraFeedLabel->setText(QStringLiteral("Switching camera\u2026"));
    connectActiveCamera();
}
