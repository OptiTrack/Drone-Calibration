#ifndef CAMERAFEEDWIDGET_H
#define CAMERAFEEDWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QComboBox>
#include <QTimer>
#include <QCamera>
#include <QMediaRecorder>
#include <QVideoWidget>
#include <QMediaCaptureSession>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QGroupBox>

QT_BEGIN_NAMESPACE
namespace Ui { class CameraFeedWidget; }
QT_END_NAMESPACE

class CameraFeedWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraFeedWidget(QWidget *parent = nullptr);
    ~CameraFeedWidget();

    void setCompactMode(bool compact);
    void setShowControls(bool show);

signals:
    void recordingSaved(const QString &filePath, const QByteArray &data);

private slots:
    void onToggleRecording();
    void onToggleFeedSource();
    void onToggleFullscreen();
    void onZoomChanged(int value);
    void onQualityChanged(const QString &quality);
    void onFormatChanged(const QString &format);
    void onFramerateChanged(int framerate);
    void onRecordingTimer();
    void onNetworkReplyFinished();
    void onCameraError();

private:
    void setupUI();
    void setupCamera();
    void setupNetworking();
    void connectSignals();
    void initializeFeed();
    void updateRecordingDisplay();
    void saveRecording();
    void loadDemoImage();
    void connectToVOXL();
    void setupSettingsPanel();
    
    Ui::CameraFeedWidget *ui;
    
    // UI Components
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_controlsLayout;
    QHBoxLayout *m_topControlsLayout;
    QHBoxLayout *m_bottomControlsLayout;
    
    QVideoWidget *m_videoWidget;
    QLabel *m_demoImageLabel;
    
    // Controls
    QPushButton *m_recordButton;
    QPushButton *m_sourceButton;
    QPushButton *m_fullscreenButton;
    QPushButton *m_settingsButton;
    
    // Settings panel
    QGroupBox *m_settingsGroup;
    QVBoxLayout *m_settingsLayout;
    QComboBox *m_qualityCombo;
    QComboBox *m_formatCombo;
    QSlider *m_framerateSlider;
    QLabel *m_framerateLabel;
    QSlider *m_zoomSlider;
    QLabel *m_zoomLabel;
    
    // Status displays
    QLabel *m_statusLabel;
    QLabel *m_recordingTimeLabel;
    QProgressBar *m_connectionProgress;
    
    // Camera and recording
    QCamera *m_camera;
    QMediaRecorder *m_mediaRecorder;
    QMediaCaptureSession *m_captureSession;
    
    // Networking for VOXL connection
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;
    
    // Timers
    QTimer *m_recordingTimer;
    
    // State
    bool m_isRecording;
    bool m_isFullscreen;
    bool m_showControls;
    bool m_compactMode;
    QString m_feedSource; // "live", "demo", "voxl"
    int m_recordingDuration;
    qint64 m_recordingStartTime;
    QString m_currentRecordingPath;
    
    // Settings
    struct CameraSettings {
        QString quality;
        QString format;
        int framerate;
        int zoom;
    } m_settings;
    
    // VOXL connection settings
    QString m_voxlHost;
    int m_voxlPort;
};

#endif // CAMERAFEEDWIDGET_H