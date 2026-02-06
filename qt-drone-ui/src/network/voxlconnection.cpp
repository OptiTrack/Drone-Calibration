#include "voxlconnection.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QHttpMultiPart>
#include <QNetworkRequest>

VOXLConnection::VOXLConnection(QObject *parent) 
    : QObject(parent)
    , m_tcpSocket(nullptr)
    , m_udpSocket(nullptr)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_connectionType(TCP_CONNECTION)
    , m_port(0)
    , m_connected(false)
    , m_connectionTimeout(5000)
    , m_heartbeatTimer(new QTimer(this))
    , m_connectionTimer(new QTimer(this))
    , m_heartbeatInterval(1000)
    , m_videoBuffer(nullptr)
    , m_videoStreamActive(false)
    , m_telemetryStreamActive(false)
    , m_videoStreamPort(8900)
    , m_telemetryStreamPort(14550)
    , m_runnerApiPort(8080)
    , m_scpProcess(nullptr)
    , m_missionApiReply(nullptr)
{
    connect(m_heartbeatTimer, &QTimer::timeout, this, &VOXLConnection::onHeartbeatTimer);
    connect(m_connectionTimer, &QTimer::timeout, this, &VOXLConnection::onConnectionTimer);
}

VOXLConnection::~VOXLConnection()
{
    disconnect();
    if (m_scpProcess) {
        m_scpProcess->kill();
        delete m_scpProcess;
    }
}

// Basic method implementations
bool VOXLConnection::connectToVOXL(const QString &host, int port, ConnectionType type)
{
    qDebug() << "Connecting to VOXL at" << host << ":" << port;
    return true; // Stub implementation
}

void VOXLConnection::disconnect()
{
    qDebug() << "Disconnecting from VOXL";
}

void VOXLConnection::sendCommand(const QString &command, const QJsonObject &params)
{
    qDebug() << "Sending command:" << command;
}

void VOXLConnection::requestStatus()
{
    qDebug() << "Requesting status";
}

// Slot implementations - minimal stubs
void VOXLConnection::onTcpConnected() {}
void VOXLConnection::onTcpDisconnected() {}
void VOXLConnection::onTcpDataReceived() {}
void VOXLConnection::onTcpError(QAbstractSocket::SocketError) {}
void VOXLConnection::onUdpDataReceived() {}
void VOXLConnection::onUdpError(QAbstractSocket::SocketError) {}
void VOXLConnection::onWebSocketConnected() {}
void VOXLConnection::onWebSocketDisconnected() {}
void VOXLConnection::onWebSocketTextMessageReceived(const QString &) {}
void VOXLConnection::onWebSocketBinaryMessageReceived(const QByteArray &) {}
void VOXLConnection::onWebSocketError(QAbstractSocket::SocketError) {}
void VOXLConnection::onHttpRequestFinished() {}
void VOXLConnection::onHttpError(QNetworkReply::NetworkError) {}
void VOXLConnection::onHeartbeatTimer() {}
void VOXLConnection::onConnectionTimer() {}

// ============================================================================
// Mission Upload and Control (VOXL2 Runner API Integration)
// ============================================================================

void VOXLConnection::uploadMissionFile(const QString &localFilePath, const QString &remotePath)
{
    if (m_voxlHost.isEmpty()) {
        emit errorOccurred("VOXL host not set. Call setVoxlHost() first.");
        return;
    }
    
    QFileInfo fileInfo(localFilePath);
    if (!fileInfo.exists()) {
        emit errorOccurred(QString("Mission file not found: %1").arg(localFilePath));
        return;
    }
    
    qDebug() << "Uploading mission file to VOXL2:" << localFilePath << "->" << remotePath;
    emit statusChanged("Uploading mission file...");
    
    // Use SCP to transfer the file to VOXL2
    // Command: scp localFilePath root@<voxl_ip>:<remotePath>
    if (m_scpProcess) {
        m_scpProcess->kill();
        delete m_scpProcess;
    }
    
    m_scpProcess = new QProcess(this);
    connect(m_scpProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VOXLConnection::onScpProcessFinished);
    connect(m_scpProcess, &QProcess::errorOccurred, 
            this, &VOXLConnection::onScpProcessError);
    
    // Build SCP command
    QString remoteTarget = QString("root@%1:%2").arg(m_voxlHost, remotePath);
    QStringList arguments;
    arguments << "-o" << "StrictHostKeyChecking=no"  // Auto-accept host key
              << "-o" << "UserKnownHostsFile=/dev/null"  // Don't save to known_hosts
              << localFilePath
              << remoteTarget;
    
    m_currentMissionFile = fileInfo.fileName();
    
    qDebug() << "Running: scp" << arguments.join(" ");
    m_scpProcess->start("scp", arguments);
}

void VOXLConnection::runMission(const QString &missionFileName)
{
    if (m_voxlHost.isEmpty()) {
        emit errorOccurred("VOXL host not set. Call setVoxlHost() first.");
        return;
    }
    
    qDebug() << "Sending run command for mission:" << missionFileName;
    emit statusChanged("Starting mission execution...");
    
    // POST to VOXL2 runner API: http://<voxl_ip>:8080/run?file=<filename>
    QUrl url(QString("http://%1:%2/run").arg(m_voxlHost).arg(m_runnerApiPort));
    QUrlQuery query;
    query.addQueryItem("file", missionFileName);
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    if (m_missionApiReply) {
        m_missionApiReply->abort();
        m_missionApiReply->deleteLater();
    }
    
    m_missionApiReply = m_networkManager->post(request, QByteArray());
    connect(m_missionApiReply, &QNetworkReply::finished,
            this, &VOXLConnection::onMissionApiReplyFinished);
    connect(m_missionApiReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &VOXLConnection::onMissionApiError);
}

void VOXLConnection::getMissionStatus()
{
    if (m_voxlHost.isEmpty()) {
        emit errorOccurred("VOXL host not set.");
        return;
    }
    
    // GET from VOXL2 runner API: http://<voxl_ip>:8080/status
    QUrl url(QString("http://%1:%2/status").arg(m_voxlHost).arg(m_runnerApiPort));
    QNetworkRequest request(url);
    
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (doc.isObject()) {
                emit missionStatusReceived(doc.object());
            }
        } else {
            qWarning() << "Mission status request failed:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void VOXLConnection::cancelMission()
{
    if (m_voxlHost.isEmpty()) {
        emit errorOccurred("VOXL host not set.");
        return;
    }
    
    qDebug() << "Cancelling mission...";
    emit statusChanged("Cancelling mission...");
    
    // POST to cancel endpoint (implementation depends on VOXL2 runner API)
    QUrl url(QString("http://%1:%2/cancel").arg(m_voxlHost).arg(m_runnerApiPort));
    QNetworkRequest request(url);
    
    QNetworkReply *reply = m_networkManager->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            emit missionCancelled();
            emit statusChanged("Mission cancelled");
        } else {
            emit errorOccurred(QString("Failed to cancel mission: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
    });
}

// ============================================================================
// Mission Upload Slots
// ============================================================================

void VOXLConnection::onScpProcessFinished(int exitCode)
{
    if (exitCode == 0) {
        qDebug() << "Mission file uploaded successfully";
        emit missionUploadComplete();
        emit statusChanged("Mission file uploaded");
    } else {
        QString errorMsg = QString("SCP upload failed with exit code %1").arg(exitCode);
        if (m_scpProcess) {
            errorMsg += QString(": %1").arg(QString::fromLocal8Bit(m_scpProcess->readAllStandardError()));
        }
        qWarning() << errorMsg;
        emit missionUploadFailed(errorMsg);
        emit errorOccurred(errorMsg);
    }
}

void VOXLConnection::onScpProcessError()
{
    QString errorMsg = "SCP process error";
    if (m_scpProcess) {
        errorMsg += QString(": %1").arg(m_scpProcess->errorString());
    }
    qWarning() << errorMsg;
    emit missionUploadFailed(errorMsg);
    emit errorOccurred(errorMsg);
}

void VOXLConnection::onMissionApiReplyFinished()
{
    if (!m_missionApiReply) return;
    
    if (m_missionApiReply->error() == QNetworkReply::NoError) {
        QByteArray responseData = m_missionApiReply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        
        qDebug() << "Mission API response:" << doc;
        emit statusChanged("Mission started successfully");
        
        if (doc.isObject()) {
            QJsonObject response = doc.object();
            if (response.contains("status")) {
                emit missionStatusReceived(response);
            }
        }
    }
    
    m_missionApiReply->deleteLater();
    m_missionApiReply = nullptr;
}

void VOXLConnection::onMissionApiError(QNetworkReply::NetworkError error)
{
    if (!m_missionApiReply) return;
    
    QString errorMsg = QString("Mission API error: %1").arg(m_missionApiReply->errorString());
    qWarning() << errorMsg;
    emit errorOccurred(errorMsg);
}