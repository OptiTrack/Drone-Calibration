#ifndef VOXLCONNECTION_H
#define VOXLCONNECTION_H

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QBuffer>
#include <QProcess>
#include <QHostAddress>
#include <QElapsedTimer>

class VOXLConnection : public QObject
{
    Q_OBJECT

public:
    enum ConnectionType {
        TCP_CONNECTION,
        UDP_CONNECTION,
        WEBSOCKET_CONNECTION,
        HTTP_REST_API
    };

    explicit VOXLConnection(QObject *parent = nullptr);
    ~VOXLConnection();

    // Connection management
    bool connectToVOXL(const QString &host, int port, ConnectionType type = TCP_CONNECTION);
    void disconnect();
    bool isConnected() const;

    // Communication
    void sendCommand(const QString &command, const QJsonObject &params = QJsonObject());
    void sendMavlinkMessage(const QByteArray &mavlinkData);
    void requestStatus();
    void requestCameraStream();
    void requestTelemetryStream();

    // Camera control
    void startVideoStream();
    void stopVideoStream();
    void startRecording();
    void stopRecording();
    void takePicture();
    void setCameraParameters(const QJsonObject &params);
    
    // Mission upload and control (VOXL2 Runner API)
    void uploadMissionFile(const QString &localFilePath, const QString &remotePath = "/data/trajectories/inbox/trajectory.json");
    void uploadFileToVoxl(const QString &localFilePath, const QString &remotePath, const QString &uploadLabel = QStringLiteral("file"));
    void downloadDirectoryFromVoxl(const QString &localDir, const QString &remotePath);
    void uploadDirectoryToVoxl(const QString &localDir, const QString &remotePath);
    void runMission(const QString &missionFileName);
    void getMissionStatus();
    void cancelMission();

    // Configuration
    void setConnectionType(ConnectionType type) { m_connectionType = type; }
    void setConnectionTimeout(int timeoutMs) { m_connectionTimeout = timeoutMs; }
    void setHeartbeatInterval(int intervalMs);
    void setVoxlHost(const QString &host) { m_voxlHost = host; }
    void setRunnerApiPort(int port) { m_runnerApiPort = port; }

signals:
    void connected();
    void disconnected();
    void dataReceived(const QJsonObject &data);
    void videoFrameReceived(const QByteArray &frameData);
    void telemetryReceived(const QJsonObject &telemetry);
    void errorOccurred(const QString &error);
    void statusChanged(const QString &status);
    
    // Mission signals
    void missionUploadProgress(int percent);
    void missionUploadComplete();
    void missionUploadFailed(const QString &error);
    void mapperBundleDownloadFinished(bool success, const QString &message);
    void mapperBundleUploadFinished(bool success, const QString &message);
    void missionStatusReceived(const QJsonObject &status);
    void missionCompleted();
    void missionCancelled();

private slots:
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpDataReceived();
    void onTcpError(QAbstractSocket::SocketError error);
    
    void onUdpDataReceived();
    void onUdpError(QAbstractSocket::SocketError error);
    
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onWebSocketTextMessageReceived(const QString &message);
    void onWebSocketBinaryMessageReceived(const QByteArray &message);
    void onWebSocketError(QAbstractSocket::SocketError error);
    
    void onHttpRequestFinished();
    void onHttpError(QNetworkReply::NetworkError error);
    
    void onHeartbeatTimer();
    void onConnectionTimer();
    
    // Mission-related slots
    void onScpProcessFinished(int exitCode);
    void onScpProcessError();
    void onMissionApiReplyFinished();
    void onMissionApiError(QNetworkReply::NetworkError error);

private:
    void initializeConnections();
    void cleanupConnections();
    void processReceivedData(const QByteArray &data);
    void processJsonMessage(const QJsonObject &json);
    void processMavlinkMessage(quint32 messageId,
                               const QByteArray &payload,
                               quint8 systemId,
                               quint8 componentId);
    void processVideoFrame(const QByteArray &frameData);
    void sendHeartbeat();
    void sendMavlinkCommandLong(quint16 command,
                                float param1 = 0.0f,
                                float param2 = 0.0f,
                                float param3 = 0.0f,
                                float param4 = 0.0f,
                                float param5 = 0.0f,
                                float param6 = 0.0f,
                                float param7 = 0.0f);
    void sendMavlinkPacket(quint32 messageId, const QByteArray &payload, quint8 crcExtra);
    QJsonObject createCommand(const QString &command, const QJsonObject &params = QJsonObject());
    
    // Connection objects
    QTcpSocket *m_tcpSocket;
    QUdpSocket *m_udpSocket;
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;
    
    // Connection state
    ConnectionType m_connectionType;
    QString m_host;
    int m_port;
    bool m_connected;
    int m_connectionTimeout;
    QHostAddress m_remoteAddress;
    quint16 m_remotePort;
    QHostAddress m_mavlinkPeerAddress;
    quint16 m_mavlinkPeerPort;
    bool m_haveMavlinkPeer;
    QElapsedTimer m_lastHeartbeatTimer;
    quint8 m_mavlinkSequence;
    quint8 m_targetSystemId;
    quint8 m_targetComponentId;
    bool m_haveAutopilotTarget;
    
    // Timers
    QTimer *m_heartbeatTimer;
    QTimer *m_connectionTimer;
    int m_heartbeatInterval;
    
    // Data buffers
    QByteArray m_dataBuffer;
    QBuffer *m_videoBuffer;
    
    // VOXL-specific settings
    QString m_voxlVersion;
    QStringList m_availableServices;
    QJsonObject m_systemInfo;
    
    // Stream management
    bool m_videoStreamActive;
    bool m_telemetryStreamActive;
    int m_videoStreamPort;
    int m_telemetryStreamPort;
    
    // Mission upload management (VOXL2 Runner API)
    QString m_voxlHost;
    int m_runnerApiPort;  // Default: 8080
    enum class ScpMode { Upload, Download, UploadDirectory };
    QProcess *m_scpProcess;
    ScpMode m_scpMode;
    QString m_scpDownloadLocalPath;
    QNetworkReply *m_missionApiReply;
    QString m_currentMissionFile;
    QString m_currentUploadLabel;
};

#endif // VOXLCONNECTION_H