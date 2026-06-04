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
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrameInput>

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
    void setVoxlHost(const QString &host);

signals:
    void recordingSaved(const QString &filePath, const QByteArray &data);

private slots:
    void onToggleRecording();
    void onToggleFullscreen();
    void onRecordingTimer();
    void onCameraSelected(int index);

private:
    void setupUI();
    void setupCamera();
    void setupNetworking();
    void connectSignals();
    void initializeFeed();
    void stopActiveFeeds();
    void saveRecording();
    void fetchCameraList();
    void connectActiveCamera();
    void connectToPortalCamera(const QUrl &url);
    void processPortalCameraBytes();
    
    Ui::CameraFeedWidget *ui;
    
    // UI Components
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_controlsLayout;
    QHBoxLayout *m_topControlsLayout;
    QHBoxLayout *m_bottomControlsLayout;
    
    QLabel *m_cameraFeedLabel;
    QLabel *m_cameraTitleLabel;
    QComboBox *m_cameraSelectCombo;

    // Controls
    QPushButton *m_recordButton;
    QPushButton *m_fullscreenButton;
    
    // Status displays
    QLabel *m_statusLabel;
    QLabel *m_recordingTimeLabel;
    QProgressBar *m_connectionProgress;
    
    // Networking for VOXL streams
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;
    QByteArray m_portalStreamBuffer;
    QStringList m_availableCameras;
    int m_currentCameraIndex;

    // MP4 recording
    QMediaCaptureSession *m_captureSession;
    QMediaRecorder *m_mediaRecorder;
    QVideoFrameInput *m_videoFrameInput;

    // Timers
    QTimer *m_recordingTimer;

    // State
    bool m_isRecording;
    bool m_isFullscreen;
    bool m_showControls;
    bool m_compactMode;
    int m_recordingDuration;
    qint64 m_recordingStartTime;
    QString m_currentRecordingPath;

    // VOXL connection settings
    QString m_voxlHost;
    int m_voxlPort;
};

#endif // CAMERAFEEDWIDGET_H
