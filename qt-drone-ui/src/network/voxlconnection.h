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

    // Configuration
    void setConnectionType(ConnectionType type) { m_connectionType = type; }
    void setConnectionTimeout(int timeoutMs) { m_connectionTimeout = timeoutMs; }
    void setHeartbeatInterval(int intervalMs);

signals:
    void connected();
    void disconnected();
    void dataReceived(const QJsonObject &data);
    void videoFrameReceived(const QByteArray &frameData);
    void telemetryReceived(const QJsonObject &telemetry);
    void errorOccurred(const QString &error);
    void statusChanged(const QString &status);

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

private:
    void initializeConnections();
    void cleanupConnections();
    void processReceivedData(const QByteArray &data);
    void processJsonMessage(const QJsonObject &json);
    void processMavlinkMessage(const QByteArray &mavlinkData);
    void processVideoFrame(const QByteArray &frameData);
    void sendHeartbeat();
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
};

#endif // VOXLCONNECTION_H