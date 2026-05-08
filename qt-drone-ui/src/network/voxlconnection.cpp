#include "voxlconnection.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QHttpMultiPart>
#include <QNetworkRequest>
#include <QDateTime>
#include <QtEndian>
#include <QtMath>
#include <cstring>

namespace {
constexpr quint8 MavlinkV1Magic = 0xFE;
constexpr quint8 MavlinkV2Magic = 0xFD;
constexpr quint8 GroundStationSystemId = 255;
constexpr quint8 GroundStationComponentId = 190; // MAV_COMP_ID_MISSIONPLANNER
constexpr quint8 MavTypeGcs = 6;
constexpr quint8 MavAutopilotInvalid = 8;
constexpr quint8 MavStateActive = 4;
constexpr int MavlinkHeartbeatTimeoutMs = 5000;

enum MavCommand : quint16 {
    MavCmdNavReturnToLaunch = 20,
    MavCmdNavLand = 21,
    MavCmdNavTakeoff = 22,
    MavCmdDoFlightTermination = 185,
    MavCmdComponentArmDisarm = 400,
};

quint16 crcAccumulate(quint8 data, quint16 crc)
{
    data ^= static_cast<quint8>(crc & 0xff);
    data ^= data << 4;
    return static_cast<quint16>((crc >> 8) ^ (static_cast<quint16>(data) << 8) ^
                                (static_cast<quint16>(data) << 3) ^
                                (static_cast<quint16>(data) >> 4));
}

quint16 mavlinkCrc(const QByteArray &data, quint8 crcExtra)
{
    quint16 crc = 0xffff;
    for (char byte : data) {
        crc = crcAccumulate(static_cast<quint8>(byte), crc);
    }
    return crcAccumulate(crcExtra, crc);
}

template <typename T>
T readLe(const QByteArray &payload, int offset)
{
    T value {};
    if (offset < 0 || offset + static_cast<int>(sizeof(T)) > payload.size()) {
        return value;
    }

    memcpy(&value, payload.constData() + offset, sizeof(T));
    return qFromLittleEndian(value);
}

float readFloatLe(const QByteArray &payload, int offset)
{
    quint32 raw = readLe<quint32>(payload, offset);
    float value = 0.0f;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

void appendLe16(QByteArray &payload, quint16 value)
{
    char bytes[sizeof(value)];
    qToLittleEndian(value, bytes);
    payload.append(bytes, sizeof(value));
}

void appendLe32(QByteArray &payload, quint32 value)
{
    char bytes[sizeof(value)];
    qToLittleEndian(value, bytes);
    payload.append(bytes, sizeof(value));
}

void appendFloatLe(QByteArray &payload, float value)
{
    quint32 raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    appendLe32(payload, raw);
}

QString px4ModeName(quint32 customMode)
{
    const quint8 mainMode = static_cast<quint8>((customMode >> 16) & 0xff);
    const quint8 subMode = static_cast<quint8>((customMode >> 24) & 0xff);

    switch (mainMode) {
    case 1: return "MANUAL";
    case 2: return "ALTCTL";
    case 3: return "POSCTL";
    case 4: return "AUTO";
    case 5: return "ACRO";
    case 6: return "OFFBOARD";
    case 7: return "STABILIZED";
    case 8: return "RATTITUDE";
    default:
        if (subMode != 0) {
            return QString("PX4 %1.%2").arg(mainMode).arg(subMode);
        }
        return QString("PX4 %1").arg(mainMode);
    }
}

QString systemStatusName(quint8 status)
{
    switch (status) {
    case 0: return "UNINIT";
    case 1: return "BOOT";
    case 2: return "CALIBRATING";
    case 3: return "STANDBY";
    case 4: return "ACTIVE";
    case 5: return "CRITICAL";
    case 6: return "EMERGENCY";
    case 7: return "POWEROFF";
    case 8: return "TERMINATING";
    default: return "UNKNOWN";
    }
}
}

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
    , m_remotePort(0)
    , m_mavlinkPeerPort(0)
    , m_haveMavlinkPeer(false)
    , m_mavlinkSequence(0)
    , m_targetSystemId(1)
    , m_targetComponentId(1)
    , m_haveAutopilotTarget(false)
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
    , m_scpMode(ScpMode::Upload)
    , m_missionApiReply(nullptr)
    , m_currentUploadLabel(QStringLiteral("file"))
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

bool VOXLConnection::connectToVOXL(const QString &host, int port, ConnectionType type)
{
    disconnect();

    m_host = host;
    m_port = port;
    m_connectionType = type;
    m_remoteAddress = QHostAddress(host);
    m_remotePort = static_cast<quint16>(port);
    m_haveMavlinkPeer = false;
    m_connected = false;
    m_haveAutopilotTarget = false;
    m_dataBuffer.clear();

    if (m_remoteAddress.isNull()) {
        emit errorOccurred(QString("Invalid VOXL host address: %1").arg(host));
        return false;
    }

    if (type != UDP_CONNECTION) {
        emit errorOccurred("Only UDP MAVLink connections are implemented for VOXL telemetry.");
        return false;
    }

    m_udpSocket = new QUdpSocket(this);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &VOXLConnection::onUdpDataReceived);
    connect(m_udpSocket, QOverload<QAbstractSocket::SocketError>::of(&QUdpSocket::errorOccurred),
            this, &VOXLConnection::onUdpError);

    bool bound = m_udpSocket->bind(QHostAddress::AnyIPv4,
                                   static_cast<quint16>(port),
                                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        bound = m_udpSocket->bind(QHostAddress::AnyIPv4, 0);
    }

    if (!bound) {
        const QString error = QString("Failed to bind UDP socket for MAVLink: %1")
                                  .arg(m_udpSocket->errorString());
        emit errorOccurred(error);
        m_udpSocket->deleteLater();
        m_udpSocket = nullptr;
        return false;
    }

    m_lastHeartbeatTimer.invalidate();
    m_heartbeatTimer->start(m_heartbeatInterval);
    m_connectionTimer->start(1000);
    sendHeartbeat();

    emit statusChanged(QString("Waiting for MAVLink heartbeat from %1:%2").arg(host).arg(port));
    return true;
}

void VOXLConnection::disconnect()
{
    m_heartbeatTimer->stop();
    m_connectionTimer->stop();

    if (m_udpSocket) {
        m_udpSocket->close();
        m_udpSocket->deleteLater();
        m_udpSocket = nullptr;
    }

    m_haveMavlinkPeer = false;

    if (m_connected) {
        m_connected = false;
        emit disconnected();
    }
}

void VOXLConnection::sendCommand(const QString &command, const QJsonObject &params)
{
    if (!m_udpSocket) {
        emit errorOccurred("Cannot send command: MAVLink UDP socket is not open.");
        return;
    }

    if (command == "heartbeat") {
        sendHeartbeat();
    } else if (command == "arm_disarm") {
        sendMavlinkCommandLong(MavCmdComponentArmDisarm, params["arm"].toBool() ? 1.0f : 0.0f);
    } else if (command == "takeoff") {
        sendMavlinkCommandLong(MavCmdNavTakeoff, 0.0f, 0.0f, 0.0f, qQNaN(), 0.0f, 0.0f,
                               static_cast<float>(params["altitude"].toDouble(10.0)));
    } else if (command == "land") {
        sendMavlinkCommandLong(MavCmdNavLand);
    } else if (command == "return_to_launch") {
        sendMavlinkCommandLong(MavCmdNavReturnToLaunch);
    } else if (command == "force_disarm" || command == "emergency_stop") {
        // MAV_CMD_COMPONENT_ARM_DISARM: disarm with param2=21196 = force (in flight, bypass normal disarm checks).
        sendMavlinkCommandLong(MavCmdComponentArmDisarm, 0.0f, 21196.0f);
    } else if (command == "flight_termination") {
        // MAV_CMD_DO_FLIGHTTERMINATION: PX4 flight termination (requires CBRK_FLIGHTTERM=0; may need power-cycle).
        sendMavlinkCommandLong(MavCmdDoFlightTermination, 1.0f);
    } else if (command == "mavlink_command_long") {
        sendMavlinkCommandLong(static_cast<quint16>(params["command"].toInt()),
                               static_cast<float>(params["param1"].toDouble()),
                               static_cast<float>(params["param2"].toDouble()),
                               static_cast<float>(params["param3"].toDouble()),
                               static_cast<float>(params["param4"].toDouble()),
                               static_cast<float>(params["param5"].toDouble()),
                               static_cast<float>(params["param6"].toDouble()),
                               static_cast<float>(params["param7"].toDouble()));
    } else {
        emit errorOccurred(QString("MAVLink command is not implemented yet: %1").arg(command));
    }
}

void VOXLConnection::requestStatus()
{
    // VOXL/PX4 streams telemetry continuously after it sees our GCS heartbeat.
    sendHeartbeat();
}

// Slot implementations
void VOXLConnection::onTcpConnected() {}
void VOXLConnection::onTcpDisconnected() {}
void VOXLConnection::onTcpDataReceived() {}
void VOXLConnection::onTcpError(QAbstractSocket::SocketError) {}
void VOXLConnection::onUdpDataReceived()
{
    while (m_udpSocket && m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (!sender.isNull()) {
            m_mavlinkPeerAddress = sender;
            m_mavlinkPeerPort = senderPort;
            m_haveMavlinkPeer = true;
        }
        processReceivedData(datagram);
    }
}

void VOXLConnection::onUdpError(QAbstractSocket::SocketError error)
{
    if (!m_udpSocket)
        return;
    // Windows reports ConnectionRefusedError when an ICMP "port unreachable" arrives for UDP; often benign after a bad send.
    if (error == QAbstractSocket::ConnectionRefusedError)
        return;
    emit errorOccurred(QString("MAVLink UDP error: %1").arg(m_udpSocket->errorString()));
}
void VOXLConnection::onWebSocketConnected() {}
void VOXLConnection::onWebSocketDisconnected() {}
void VOXLConnection::onWebSocketTextMessageReceived(const QString &) {}
void VOXLConnection::onWebSocketBinaryMessageReceived(const QByteArray &) {}
void VOXLConnection::onWebSocketError(QAbstractSocket::SocketError) {}
void VOXLConnection::onHttpRequestFinished() {}
void VOXLConnection::onHttpError(QNetworkReply::NetworkError) {}
void VOXLConnection::onHeartbeatTimer()
{
    sendHeartbeat();
}

void VOXLConnection::onConnectionTimer()
{
    if (!m_connected || !m_lastHeartbeatTimer.isValid()) {
        return;
    }

    if (m_lastHeartbeatTimer.elapsed() > MavlinkHeartbeatTimeoutMs) {
        m_connected = false;
        emit disconnected();
        emit statusChanged("MAVLink heartbeat timed out");
    }
}

bool VOXLConnection::isConnected() const
{
    return m_connected;
}

void VOXLConnection::sendMavlinkMessage(const QByteArray &mavlinkData)
{
    if (!m_udpSocket) {
        return;
    }

    const QHostAddress dest = m_haveMavlinkPeer ? m_mavlinkPeerAddress : m_remoteAddress;
    const quint16 destPort = m_haveMavlinkPeer ? m_mavlinkPeerPort : m_remotePort;
    if (dest.isNull())
        return;

    m_udpSocket->writeDatagram(mavlinkData, dest, destPort);
}

void VOXLConnection::processReceivedData(const QByteArray &data)
{
    m_dataBuffer.append(data);

    while (!m_dataBuffer.isEmpty()) {
        const quint8 magic = static_cast<quint8>(m_dataBuffer.at(0));
        if (magic != MavlinkV1Magic && magic != MavlinkV2Magic) {
            m_dataBuffer.remove(0, 1);
            continue;
        }

        const bool mavlink2 = magic == MavlinkV2Magic;
        const int headerLength = mavlink2 ? 10 : 6;
        if (m_dataBuffer.size() < headerLength) {
            return;
        }

        const int payloadLength = static_cast<quint8>(m_dataBuffer.at(1));
        const bool signedPacket = mavlink2 && (static_cast<quint8>(m_dataBuffer.at(2)) & 0x01);
        const int signatureLength = signedPacket ? 13 : 0;
        const int packetLength = headerLength + payloadLength + 2 + signatureLength;
        if (m_dataBuffer.size() < packetLength) {
            return;
        }

        QByteArray packet = m_dataBuffer.left(packetLength);
        m_dataBuffer.remove(0, packetLength);

        quint32 messageId = 0;
        quint8 systemId = 0;
        quint8 componentId = 0;
        QByteArray payload;
        if (mavlink2) {
            systemId = static_cast<quint8>(packet.at(5));
            componentId = static_cast<quint8>(packet.at(6));
            messageId = static_cast<quint8>(packet.at(7)) |
                        (static_cast<quint8>(packet.at(8)) << 8) |
                        (static_cast<quint8>(packet.at(9)) << 16);
            payload = packet.mid(10, payloadLength);
        } else {
            systemId = static_cast<quint8>(packet.at(3));
            componentId = static_cast<quint8>(packet.at(4));
            messageId = static_cast<quint8>(packet.at(5));
            payload = packet.mid(6, payloadLength);
        }

        processMavlinkMessage(messageId, payload, systemId, componentId);
    }
}

void VOXLConnection::processMavlinkMessage(quint32 messageId,
                                           const QByteArray &payload,
                                           quint8 systemId,
                                           quint8 componentId)
{
    QJsonObject status;

    switch (messageId) {
    case 0: { // HEARTBEAT
        const quint32 customMode = readLe<quint32>(payload, 0);
        const quint8 vehicleType = readLe<quint8>(payload, 4);
        const quint8 autopilot = readLe<quint8>(payload, 5);
        const quint8 baseMode = readLe<quint8>(payload, 6);
        const quint8 systemStatus = readLe<quint8>(payload, 7);

        if (vehicleType == MavTypeGcs || autopilot == MavAutopilotInvalid) {
            return;
        }

        m_targetSystemId = systemId;
        m_targetComponentId = componentId;
        m_haveAutopilotTarget = true;
        m_lastHeartbeatTimer.restart();
        if (!m_connected) {
            m_connected = true;
            emit connected();
        }

        status["flightMode"] = px4ModeName(customMode);
        status["armed"] = (baseMode & 0x80) != 0;
        status["systemStatus"] = systemStatusName(systemStatus);
        status["lastHeartbeat"] = QDateTime::currentDateTime().toString("hh:mm:ss");
        break;
    }
    case 1: { // SYS_STATUS
        if (m_haveAutopilotTarget && systemId != m_targetSystemId) {
            return;
        }

        const quint16 millivolts = readLe<quint16>(payload, 14);
        const qint8 remaining = static_cast<qint8>(readLe<quint8>(payload, 30));
        if (millivolts != UINT16_MAX && millivolts > 0) {
            status["batteryVoltage"] = millivolts / 1000.0;
        }
        if (remaining >= 0 && remaining <= 100) {
            status["batteryPercentage"] = remaining;
        }
        break;
    }
    case 24: { // GPS_RAW_INT
        if (m_haveAutopilotTarget && systemId != m_targetSystemId) {
            return;
        }

        const qint32 lat = readLe<qint32>(payload, 8);
        const qint32 lon = readLe<qint32>(payload, 12);
        const qint32 alt = readLe<qint32>(payload, 16);
        const quint8 fixType = readLe<quint8>(payload, 28);
        const quint8 satellites = readLe<quint8>(payload, 29);
        status["gpsLock"] = fixType >= 3;
        status["gpsNumSats"] = satellites;
        status["latitude"] = lat / 10000000.0;
        status["longitude"] = lon / 10000000.0;
        status["gpsAltitude"] = alt / 1000.0;
        break;
    }
    case 30: { // ATTITUDE
        if (m_haveAutopilotTarget && systemId != m_targetSystemId) {
            return;
        }

        status["roll"] = qRadiansToDegrees(readFloatLe(payload, 4));
        status["pitch"] = qRadiansToDegrees(readFloatLe(payload, 8));
        status["yaw"] = qRadiansToDegrees(readFloatLe(payload, 12));
        break;
    }
    case 32: { // LOCAL_POSITION_NED
        if (m_haveAutopilotTarget && systemId != m_targetSystemId) {
            return;
        }

        status["velocityX"] = readFloatLe(payload, 16);
        status["velocityY"] = readFloatLe(payload, 20);
        status["velocityZ"] = readFloatLe(payload, 24);
        status["localAltitude"] = -readFloatLe(payload, 12);
        break;
    }
    case 33: { // GLOBAL_POSITION_INT
        if (m_haveAutopilotTarget && systemId != m_targetSystemId) {
            return;
        }

        const qint32 lat = readLe<qint32>(payload, 4);
        const qint32 lon = readLe<qint32>(payload, 8);
        const qint32 relativeAlt = readLe<qint32>(payload, 16);
        const qint16 vx = readLe<qint16>(payload, 20);
        const qint16 vy = readLe<qint16>(payload, 22);
        const qint16 vz = readLe<qint16>(payload, 24);
        status["latitude"] = lat / 10000000.0;
        status["longitude"] = lon / 10000000.0;
        status["altitude"] = relativeAlt / 1000.0;
        status["velocityX"] = vx / 100.0;
        status["velocityY"] = vy / 100.0;
        status["velocityZ"] = vz / 100.0;
        break;
    }
    case 74: { // VFR_HUD
        if (m_haveAutopilotTarget && systemId != m_targetSystemId) {
            return;
        }

        status["groundSpeed"] = readFloatLe(payload, 4);
        status["altitude"] = readFloatLe(payload, 12);
        status["verticalSpeed"] = readFloatLe(payload, 16);
        break;
    }
    case 147: { // BATTERY_STATUS
        if (m_haveAutopilotTarget && systemId != m_targetSystemId) {
            return;
        }

        int validCells = 0;
        int totalMillivolts = 0;
        for (int i = 0; i < 10; ++i) {
            const quint16 cellMillivolts = readLe<quint16>(payload, 10 + (i * 2));
            if (cellMillivolts > 0 && cellMillivolts != UINT16_MAX) {
                totalMillivolts += cellMillivolts;
                ++validCells;
            }
        }
        const qint8 remaining = static_cast<qint8>(readLe<quint8>(payload, 35));
        if (validCells > 0) {
            status["batteryVoltage"] = totalMillivolts / 1000.0;
        }
        if (remaining >= 0 && remaining <= 100) {
            status["batteryPercentage"] = remaining;
        }
        break;
    }
    case 77: { // COMMAND_ACK
        const quint16 command = readLe<quint16>(payload, 0);
        const quint8 result = readLe<quint8>(payload, 2);
        emit statusChanged(QString("MAVLink command %1 acknowledged with result %2").arg(command).arg(result));
        break;
    }
    default:
        break;
    }

    if (!status.isEmpty()) {
        emit dataReceived(QJsonObject{{"type", "status"}, {"data", status}});
        emit telemetryReceived(status);
    }
}

void VOXLConnection::sendHeartbeat()
{
    if (!m_udpSocket || m_remoteAddress.isNull()) {
        return;
    }

    QByteArray payload;
    appendLe32(payload, 0); // custom_mode
    payload.append(static_cast<char>(MavTypeGcs));
    payload.append(static_cast<char>(MavAutopilotInvalid));
    payload.append(static_cast<char>(0));
    payload.append(static_cast<char>(MavStateActive));
    payload.append(static_cast<char>(3));

    sendMavlinkPacket(0, payload, 50);
}

void VOXLConnection::sendMavlinkCommandLong(quint16 command,
                                            float param1,
                                            float param2,
                                            float param3,
                                            float param4,
                                            float param5,
                                            float param6,
                                            float param7)
{
    QByteArray payload;
    appendFloatLe(payload, param1);
    appendFloatLe(payload, param2);
    appendFloatLe(payload, param3);
    appendFloatLe(payload, param4);
    appendFloatLe(payload, param5);
    appendFloatLe(payload, param6);
    appendFloatLe(payload, param7);
    appendLe16(payload, command);
    payload.append(static_cast<char>(m_targetSystemId));
    payload.append(static_cast<char>(m_targetComponentId));
    payload.append(static_cast<char>(0)); // confirmation

    sendMavlinkPacket(76, payload, 152);
}

void VOXLConnection::sendMavlinkPacket(quint32 messageId, const QByteArray &payload, quint8 crcExtra)
{
    QByteArray header;
    header.append(static_cast<char>(MavlinkV2Magic));
    header.append(static_cast<char>(payload.size()));
    header.append(static_cast<char>(0)); // incompat flags
    header.append(static_cast<char>(0)); // compat flags
    header.append(static_cast<char>(m_mavlinkSequence++));
    header.append(static_cast<char>(GroundStationSystemId));
    header.append(static_cast<char>(GroundStationComponentId));
    header.append(static_cast<char>(messageId & 0xff));
    header.append(static_cast<char>((messageId >> 8) & 0xff));
    header.append(static_cast<char>((messageId >> 16) & 0xff));

    QByteArray crcInput = header.mid(1);
    crcInput.append(payload);
    const quint16 crc = mavlinkCrc(crcInput, crcExtra);

    QByteArray packet = header;
    packet.append(payload);
    appendLe16(packet, crc);

    sendMavlinkMessage(packet);
}

void VOXLConnection::processJsonMessage(const QJsonObject &json)
{
    emit dataReceived(json);
}

void VOXLConnection::processVideoFrame(const QByteArray &frameData)
{
    emit videoFrameReceived(frameData);
}

QJsonObject VOXLConnection::createCommand(const QString &command, const QJsonObject &params)
{
    return QJsonObject{{"command", command}, {"params", params}};
}

// ============================================================================
// Mission Upload and Control (VOXL2 Runner API Integration)
// ============================================================================

void VOXLConnection::uploadMissionFile(const QString &localFilePath, const QString &remotePath)
{
    uploadFileToVoxl(localFilePath, remotePath, QStringLiteral("mission file"));
}

void VOXLConnection::uploadFileToVoxl(const QString &localFilePath, const QString &remotePath, const QString &uploadLabel)
{
    m_scpMode = ScpMode::Upload;
    if (m_voxlHost.isEmpty()) {
        emit errorOccurred("VOXL host not set. Call setVoxlHost() first.");
        return;
    }
    
    QFileInfo fileInfo(localFilePath);
    if (!fileInfo.exists()) {
        emit errorOccurred(QString("Upload file not found: %1").arg(localFilePath));
        return;
    }
    
    const QString label = uploadLabel.trimmed().isEmpty() ? QStringLiteral("file") : uploadLabel.trimmed();
    qDebug() << "Uploading" << label << "to VOXL2:" << localFilePath << "->" << remotePath;
    emit statusChanged(QString("Uploading %1...").arg(label));
    
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
    m_currentUploadLabel = label;
    
    qDebug() << "Running: scp" << arguments.join(" ");
    m_scpProcess->start("scp", arguments);
}

void VOXLConnection::downloadDirectoryFromVoxl(const QString &localDir, const QString &remotePath)
{
    if (m_voxlHost.isEmpty()) {
        emit mapperBundleDownloadFinished(false, QStringLiteral("VOXL host not set."));
        return;
    }
    const QString cleanRemote = remotePath.trimmed();
    if (cleanRemote.isEmpty()) {
        emit mapperBundleDownloadFinished(false, QStringLiteral("Remote map path is empty."));
        return;
    }

    const QString nativeLocal = QDir::toNativeSeparators(localDir);
    const QFileInfo localInfo(nativeLocal);
    QDir().mkpath(localInfo.absolutePath());
    if (QDir(nativeLocal).exists())
        QDir(nativeLocal).removeRecursively();

    if (m_scpProcess) {
        m_scpProcess->kill();
        m_scpProcess->deleteLater();
        m_scpProcess = nullptr;
    }

    m_scpProcess = new QProcess(this);
    m_scpMode = ScpMode::Download;
    m_scpDownloadLocalPath = nativeLocal;
    connect(m_scpProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VOXLConnection::onScpProcessFinished);
    connect(m_scpProcess, &QProcess::errorOccurred,
            this, &VOXLConnection::onScpProcessError);

    QString remoteArg = QStringLiteral("root@%1:%2").arg(m_voxlHost, QDir::fromNativeSeparators(cleanRemote));
    if (!remoteArg.endsWith(QLatin1Char('/')))
        remoteArg += QLatin1Char('/');

    QStringList arguments;
    arguments << QStringLiteral("-r")
              << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=no")
              << QStringLiteral("-o") << QStringLiteral("UserKnownHostsFile=/dev/null")
              << remoteArg
              << nativeLocal;

    emit statusChanged(QStringLiteral("Downloading mapper map from VOXL via scp…"));
    m_scpProcess->start(QStringLiteral("scp"), arguments);
}

void VOXLConnection::uploadDirectoryToVoxl(const QString &localDir, const QString &remotePath)
{
    if (m_voxlHost.isEmpty()) {
        emit mapperBundleUploadFinished(false, QStringLiteral("VOXL host not set."));
        return;
    }
    const QString cleanLocal = QDir::cleanPath(localDir);
    if (cleanLocal.isEmpty() || !QDir(cleanLocal).exists()) {
        emit mapperBundleUploadFinished(false, QStringLiteral("Local map bundle folder not found."));
        return;
    }
    const QString cleanRemote = remotePath.trimmed();
    if (cleanRemote.isEmpty()) {
        emit mapperBundleUploadFinished(false, QStringLiteral("Remote map path is empty."));
        return;
    }

    if (m_scpProcess) {
        m_scpProcess->kill();
        m_scpProcess->deleteLater();
        m_scpProcess = nullptr;
    }

    m_scpProcess = new QProcess(this);
    m_scpMode = ScpMode::UploadDirectory;
    connect(m_scpProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VOXLConnection::onScpProcessFinished);
    connect(m_scpProcess, &QProcess::errorOccurred,
            this, &VOXLConnection::onScpProcessError);

    QString localArg = QDir::fromNativeSeparators(cleanLocal);
    if (!localArg.endsWith(QLatin1String("/.")))
        localArg += QStringLiteral("/.");

    QString remoteArg = QStringLiteral("root@%1:%2").arg(m_voxlHost, QDir::fromNativeSeparators(cleanRemote));
    if (!remoteArg.endsWith(QLatin1Char('/')))
        remoteArg += QLatin1Char('/');

    QStringList arguments;
    arguments << QStringLiteral("-r")
              << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=no")
              << QStringLiteral("-o") << QStringLiteral("UserKnownHostsFile=/dev/null")
              << localArg
              << remoteArg;

    emit statusChanged(QStringLiteral("Uploading mapper map bundle to VOXL via scp…"));
    m_scpProcess->start(QStringLiteral("scp"), arguments);
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
    const bool isDownload = (m_scpMode == ScpMode::Download);
    const bool isUploadDir = (m_scpMode == ScpMode::UploadDirectory);
    if (exitCode == 0) {
        if (isDownload) {
            m_scpMode = ScpMode::Upload;
            emit statusChanged(QStringLiteral("Mapper map copied from VOXL."));
            emit mapperBundleDownloadFinished(true, m_scpDownloadLocalPath);
            return;
        }
        if (isUploadDir) {
            m_scpMode = ScpMode::Upload;
            emit statusChanged(QStringLiteral("Mapper map bundle uploaded to VOXL."));
            emit mapperBundleUploadFinished(true, QString());
            return;
        }
        const QString label = m_currentUploadLabel.isEmpty() ? QStringLiteral("file") : m_currentUploadLabel;
        qDebug() << label << "uploaded successfully";
        emit missionUploadComplete();
        emit statusChanged(QString("%1 uploaded").arg(label.left(1).toUpper() + label.mid(1)));
    } else {
        if (isDownload) {
            QString errorMsg = QStringLiteral("SCP download failed with exit code %1").arg(exitCode);
            if (m_scpProcess)
                errorMsg += QStringLiteral(": %1").arg(QString::fromLocal8Bit(m_scpProcess->readAllStandardError()));
            qWarning() << errorMsg;
            m_scpMode = ScpMode::Upload;
            emit mapperBundleDownloadFinished(false, errorMsg);
            return;
        }
        if (isUploadDir) {
            QString errorMsg = QStringLiteral("SCP bundle upload failed with exit code %1").arg(exitCode);
            if (m_scpProcess)
                errorMsg += QStringLiteral(": %1").arg(QString::fromLocal8Bit(m_scpProcess->readAllStandardError()));
            qWarning() << errorMsg;
            m_scpMode = ScpMode::Upload;
            emit mapperBundleUploadFinished(false, errorMsg);
            return;
        }
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
    const bool isDownload = (m_scpMode == ScpMode::Download);
    const bool isUploadDir = (m_scpMode == ScpMode::UploadDirectory);
    QString errorMsg = QStringLiteral("SCP process error");
    if (m_scpProcess) {
        errorMsg += QString(": %1").arg(m_scpProcess->errorString());
    }
    qWarning() << errorMsg;
    if (isDownload) {
        m_scpMode = ScpMode::Upload;
        emit mapperBundleDownloadFinished(false, errorMsg);
        return;
    }
    if (isUploadDir) {
        m_scpMode = ScpMode::Upload;
        emit mapperBundleUploadFinished(false, errorMsg);
        return;
    }
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