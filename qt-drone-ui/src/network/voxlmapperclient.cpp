#include "voxlmapperclient.h"

#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QtEndian>
#include <QtMath>
#include <cstring>
#include <cmath>
#include <limits>

namespace {
constexpr quint64 MaxWebSocketPayloadBytes = 25ULL * 1024ULL * 1024ULL;
constexpr quint32 MaxRenderPointsPerPacket = 200000;
constexpr quint32 MaxMeshVerticesPerPacket = 300000;
constexpr quint32 MaxMeshTrianglesPerPacket = 500000;

template <typename T>
T readLe(const QByteArray &payload, int offset)
{
    T value {};
    if (offset < 0 || offset + static_cast<int>(sizeof(T)) > payload.size())
        return value;
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

QString commandNumber(float value)
{
    return QString::number(static_cast<double>(value), 'f', 3);
}

QByteArray websocketKey()
{
    QByteArray key;
    key.resize(16);
    for (char &byte : key)
        byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return key.toBase64();
}
}

VOXLMapperClient::VOXLMapperClient(QObject *parent)
    : QObject(parent)
    , m_port(80)
    , m_planReady(false)
    , m_meshReady(false)
    , m_poseReady(false)
    , m_connected(false)
    , m_poseReconnectTimer(new QTimer(this))
    , m_hasLastPose(false)
    , m_lastPoseFrd()
{
    m_poseReconnectTimer->setSingleShot(true);
    m_poseReconnectTimer->setInterval(2000);
    connect(m_poseReconnectTimer, &QTimer::timeout, this, &VOXLMapperClient::reconnectPose);

    auto wireSocket = [this](QTcpSocket &socket, const QString &path, QByteArray &buffer, bool &ready, const QString &name) {
        QTcpSocket *socketPtr = &socket;
        QByteArray *bufferPtr = &buffer;
        bool *readyPtr = &ready;

        connect(socketPtr, &QTcpSocket::connected, this, [this, socketPtr, path, name]() {
            sendHandshake(*socketPtr, path);
            emit statusChanged(QString("VOXL Mapper %1 socket connected").arg(name));
        });
        connect(socketPtr, &QTcpSocket::disconnected, this, [this, bufferPtr, readyPtr, name]() {
            bufferPtr->clear();
            *readyPtr = false;
            emit statusChanged(QString("VOXL Mapper %1 socket disconnected").arg(name));
            if (name == QLatin1String("mesh"))
                emit meshConnectedChanged(false);
            if (name == QLatin1String("pose") && !m_host.isEmpty())
                m_poseReconnectTimer->start(); // auto-reconnect pose after 2 s
            updateConnectedState();
        });
        connect(socketPtr, &QTcpSocket::readyRead, this, [this, socketPtr, bufferPtr, readyPtr, name]() {
            handleSocketData(*socketPtr, *bufferPtr, *readyPtr, name);
        });
        connect(socketPtr, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                this, [this, socketPtr, name](QAbstractSocket::SocketError) {
                    emit errorOccurred(QString("VOXL Mapper %1 socket error: %2").arg(name, socketPtr->errorString()));
                });
    };

    wireSocket(m_planSocket, QStringLiteral("/plan"), m_planBuffer, m_planReady, QStringLiteral("plan"));
    wireSocket(m_meshSocket, QStringLiteral("/mesh"), m_meshBuffer, m_meshReady, QStringLiteral("mesh"));
    wireSocket(m_poseSocket, QStringLiteral("/pose"), m_poseBuffer, m_poseReady, QStringLiteral("pose"));
}

void VOXLMapperClient::connectToMapper(const QString &host, int port)
{
    m_host = host;
    m_port = port;
    disconnectFromMapper();

    // voxl-portal (Node.js) initializes its backend proxy to voxl-mapper
    // lazily — only after it receives a plain HTTP request on port 80.
    // We send GET / and wait for the HTTP response headers before opening
    // WebSocket channels, so we KNOW the portal is ready. A 3 s fallback
    // timer opens sockets anyway if the response never arrives.
    QTcpSocket *wakeSocket = new QTcpSocket(this);

    // Shared one-shot flag so only one path (response or timeout) opens sockets.
    auto opened = std::make_shared<bool>(false);

    auto openAll = [this, host, port, opened]() {
        if (*opened) return;
        *opened = true;
        if (m_host != host || m_port != port) return;
        openSocket(m_planSocket, QStringLiteral("/plan"));
        openSocket(m_meshSocket, QStringLiteral("/mesh"));
        openSocket(m_poseSocket, QStringLiteral("/pose"));
    };

    connect(wakeSocket, &QTcpSocket::connected, this, [wakeSocket, host, port]() {
        wakeSocket->write(
            "GET / HTTP/1.1\r\n"
            "Host: " + host.toLatin1() + ":" + QByteArray::number(port) + "\r\n"
            "Connection: close\r\n\r\n");
    });
    // Open sockets as soon as we receive any HTTP response bytes (portal is up).
    connect(wakeSocket, &QTcpSocket::readyRead, this, [wakeSocket, openAll]() {
        wakeSocket->readAll(); // discard response body
        wakeSocket->disconnectFromHost();
        openAll();
    });
    connect(wakeSocket, &QTcpSocket::disconnected, wakeSocket, &QObject::deleteLater);
    connect(wakeSocket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, [wakeSocket, openAll](QAbstractSocket::SocketError) {
                wakeSocket->deleteLater();
                openAll(); // try anyway if TCP fails
            });
    wakeSocket->connectToHost(host, static_cast<quint16>(port));

    // Fallback: open after 3 s in case the HTTP response is very slow.
    QTimer::singleShot(3000, this, [openAll]() { openAll(); });
}

void VOXLMapperClient::disconnectFromMapper()
{
    m_poseReconnectTimer->stop(); // don't reconnect after an intentional disconnect
    m_planSocket.disconnectFromHost();
    m_meshSocket.disconnectFromHost();
    m_poseSocket.disconnectFromHost();
    m_planReady = false;
    m_meshReady = false;
    m_poseReady = false;
    m_planBuffer.clear();
    m_meshBuffer.clear();
    m_poseBuffer.clear();
    m_hasLastPose = false;
    updateConnectedState();
}

bool VOXLMapperClient::isConnected() const
{
    return m_planReady && m_meshReady;
}

void VOXLMapperClient::reconnectMesh()
{
    // Close the stale mesh socket and reopen it.  Plan and pose sockets are
    // left untouched so active path-following is not interrupted.
    m_meshSocket.disconnectFromHost();
    m_meshReady = false;
    m_meshBuffer.clear();
    openSocket(m_meshSocket, QStringLiteral("/mesh"));
}

void VOXLMapperClient::reconnectPose()
{
    if (m_host.isEmpty())
        return;
    m_poseSocket.disconnectFromHost();
    // The disconnected signal fires synchronously above and restarts the timer;
    // stop it immediately so we don't kill the new socket 2 seconds from now.
    m_poseReconnectTimer->stop();
    m_poseReady = false;
    m_poseBuffer.clear();
    m_hasLastPose = false; // reset sanity filter baseline after reconnect
    emit statusChanged(QStringLiteral("VOXL Mapper pose socket reconnecting..."));
    openSocket(m_poseSocket, QStringLiteral("/pose"));
}

void VOXLMapperClient::planToFrd(const QVector3D &targetFrd)
{
    sendPlanCommand(QStringLiteral("plan_to: %1,%2,%3")
                        .arg(commandNumber(targetFrd.x()),
                             commandNumber(targetFrd.y()),
                             commandNumber(targetFrd.z())));
}

void VOXLMapperClient::planHome()
{
    // voxl-mapper / voxl-portal: "Plan Home" plans from current pose toward the mapper home goal.
    // README documents that goal as (0,0,-1.5) in the planner frame (1.5 m above the VIO/map origin).
    // See: https://gitlab.com/voxl-public/voxl-sdk/services/voxl-mapper/-/blob/master/README.md (Viewing → Plan Home)
    sendPlanCommand(QStringLiteral("plan_home"));
}

void VOXLMapperClient::followPath()
{
    sendPlanCommand(QStringLiteral("follow_path"));
}

void VOXLMapperClient::stopFollowing()
{
    sendPlanCommand(QStringLiteral("stop_following"));
}

void VOXLMapperClient::clearMap()
{
    sendMeshCommand(QStringLiteral("clear_map"));
}

void VOXLMapperClient::restartMapper()
{
    if (m_host.trimmed().isEmpty())
        return;
    QNetworkAccessManager *nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(QStringLiteral("http://%1:8099/restart-voxl-mapper").arg(m_host)));
    req.setRawHeader("X-Restart-Token", "change-this-token");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = nam->post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        if (reply->error() == QNetworkReply::NoError)
            emit statusChanged(QStringLiteral("voxl-mapper restart requested — sockets will reconnect"));
        else
            emit errorOccurred(QStringLiteral("Mapper restart request failed: %1").arg(reply->errorString()));
        reply->deleteLater();
        nam->deleteLater();
    });
}

void VOXLMapperClient::resetVio()
{
    sendMeshCommand(QStringLiteral("reset_vio"));
}

void VOXLMapperClient::resetQvio()
{
    triggerResetEndpoint(QStringLiteral("/reset_qvio/"));
}

void VOXLMapperClient::resetOv()
{
    triggerResetEndpoint(QStringLiteral("/reset_ov/"));
}

void VOXLMapperClient::triggerResetEndpoint(const QString &path)
{
    // Mirrors VOXL Portal vio.js: open a new WebSocket to /reset_qvio/ or
    // /reset_ov/ — the act of connecting triggers the reset on the drone.
    // No message needs to be sent; we disconnect shortly after the handshake.
    if (m_host.trimmed().isEmpty())
        return;

    QTcpSocket *socket = new QTcpSocket(nullptr); // no parent; managed via deleteLater

    connect(socket, &QTcpSocket::connected, this, [this, socket, path]() {
        sendHandshake(*socket, path);
        // Give the handshake time to be flushed, then disconnect
        QTimer::singleShot(500, socket, &QTcpSocket::disconnectFromHost);
    });

    // Clean up when the socket closes normally
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

    // Clean up (and report) on error
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, [this, socket, path](QAbstractSocket::SocketError) {
                emit errorOccurred(
                    QStringLiteral("VIO reset failed (%1): %2").arg(path, socket->errorString()));
                socket->deleteLater();
            });

    socket->connectToHost(m_host, static_cast<quint16>(m_port));
}

void VOXLMapperClient::loadMap(const QString &remotePath)
{
    QString command = QStringLiteral("load_map");
    if (!remotePath.trimmed().isEmpty())
        command += QStringLiteral(" file: ") + remotePath.trimmed();
    sendMeshCommand(command);
}

void VOXLMapperClient::saveMap(const QString &format, const QString &remotePath)
{
    const QString fmt = format.trimmed().isEmpty() ? QStringLiteral("ply") : format.trimmed();
    QString command = QStringLiteral("save_map:%1").arg(fmt);
    if (!remotePath.trimmed().isEmpty())
        command += QStringLiteral(" file: ") + remotePath.trimmed();
    sendMeshCommand(command);
}

void VOXLMapperClient::setCostmapSlice(float sliceLevel)
{
    sendMeshCommand(QStringLiteral("slice_level:%1").arg(commandNumber(sliceLevel)));
}

void VOXLMapperClient::openSocket(QTcpSocket &socket, const QString &)
{
    if (m_host.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("VOXL host is not set for mapper connection."));
        return;
    }
    socket.connectToHost(m_host, static_cast<quint16>(m_port));
}

void VOXLMapperClient::sendHandshake(QTcpSocket &socket, const QString &path)
{
    const QByteArray host = m_host.toLatin1() + ":" + QByteArray::number(m_port);
    const QByteArray request =
        "GET " + path.toLatin1() + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Origin: http://" + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: " + websocketKey() + "\r\n\r\n";
    socket.write(request);
}

void VOXLMapperClient::handleSocketData(QTcpSocket &socket, QByteArray &buffer, bool &ready, const QString &name)
{
    buffer.append(socket.readAll());

    if (!ready) {
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = buffer.left(headerEnd);
        if (!header.startsWith("HTTP/1.1 101") && !header.startsWith("HTTP/1.0 101")) {
            emit errorOccurred(QString("VOXL Mapper %1 websocket upgrade failed: %2")
                                   .arg(name, QString::fromLatin1(header.left(80))));
            socket.disconnectFromHost();
            return;
        }
        buffer.remove(0, headerEnd + 4);
        ready = true;
        if (name == QLatin1String("mesh"))
            emit meshConnectedChanged(true);
        updateConnectedState();
    }

    int decodeBudget = 0;
    while (buffer.size() >= 2) {
        if (++decodeBudget > 256) {
            qWarning() << "VOXL Mapper" << name << "websocket: stopping after 256 frame ops per read (buffer"
                       << buffer.size() << "bytes).";
            return;
        }
        const quint8 b0 = static_cast<quint8>(buffer.at(0));
        const quint8 b1 = static_cast<quint8>(buffer.at(1));
        const quint8 opcode = b0 & 0x0f;
        quint64 payloadLength = b1 & 0x7f;
        int headerLength = 2;

        if (payloadLength == 126) {
            if (buffer.size() < 4)
                return;
            payloadLength = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(buffer.constData() + 2));
            headerLength = 4;
        } else if (payloadLength == 127) {
            if (buffer.size() < 10)
                return;
            payloadLength = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(buffer.constData() + 2));
            headerLength = 10;
        }

        const bool masked = (b1 & 0x80) != 0;
        const int maskOffset = headerLength;
        if (masked)
            headerLength += 4;
        if (payloadLength > static_cast<quint64>(std::numeric_limits<int>::max()) ||
            payloadLength > MaxWebSocketPayloadBytes) {
            // Misaligned reads often decode a pose timestamp (ns) as a 64-bit "extended length"
            // — values ~1e15–1e18 are typical monotonic clock nanoseconds, not byte counts.
            const int scanLimit = qMin(buffer.size(), 8192);
            int syncAt = -1;
            for (int i = 1; i < scanLimit; ++i) {
                const quint8 c = static_cast<quint8>(buffer.at(i));
                if ((c & 0x0f) == 0x02) {
                    syncAt = i;
                    break;
                }
            }
            if (syncAt < 0) {
                for (int i = 0; i < scanLimit; ++i) {
                    const quint8 c = static_cast<quint8>(buffer.at(i));
                    if (c == 0x82 || c == 0x02) {
                        syncAt = i;
                        break;
                    }
                }
            }
            if (syncAt > 0) {
                qWarning() << "VOXL Mapper" << name
                           << "websocket: dropped" << syncAt
                           << "byte(s) to resync (invalid declared length" << payloadLength << ").";
                buffer.remove(0, syncAt);
                continue;
            }
            if (buffer.size() > 1) {
                buffer.remove(0, 1);
                continue;
            }
            emit errorOccurred(QStringLiteral(
                                   "VOXL Mapper %1 websocket framing lost sync (invalid length %2). Disconnecting.")
                                   .arg(name)
                                   .arg(payloadLength));
            socket.disconnectFromHost();
            return;
        }
        if (buffer.size() < headerLength + static_cast<int>(payloadLength))
            return;

        QByteArray payload = buffer.mid(headerLength, static_cast<int>(payloadLength));
        if (masked) {
            const QByteArray mask = buffer.mid(maskOffset, 4);
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = payload[i] ^ mask.at(i % 4);
        }
        buffer.remove(0, headerLength + static_cast<int>(payloadLength));

        if (opcode == 0x8) {
            socket.disconnectFromHost();
            return;
        }
        if (opcode == 0x9) {
            // WebSocket ping — respond with pong (RFC 6455 §5.5.3) echoing the payload.
            // Required: servers that send pings will close the connection if no pong arrives.
            QByteArray pongFrame;
            pongFrame.append(static_cast<char>(0x8A)); // FIN=1, opcode=0xA (pong)
            pongFrame.append(static_cast<char>(static_cast<int>(payloadLength) & 0x7F));
            if (!payload.isEmpty())
                pongFrame.append(payload);
            socket.write(pongFrame);
            continue;
        }
        if (opcode != 0x2)
            continue;

        if (name == QLatin1String("pose"))
            handlePoseMessage(payload);
        else if (name == QLatin1String("plan"))
            handlePlanMessage(payload);
        else if (name == QLatin1String("mesh"))
            handleMeshMessage(payload);
    }
}

void VOXLMapperClient::sendWebSocketText(QTcpSocket &socket, const QString &command)
{
    const QByteArray payload = command.toUtf8();
    QByteArray frame;
    frame.append(static_cast<char>(0x81));

    if (payload.size() < 126) {
        frame.append(static_cast<char>(0x80 | payload.size()));
    } else {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((payload.size() >> 8) & 0xff));
        frame.append(static_cast<char>(payload.size() & 0xff));
    }

    QByteArray mask;
    mask.resize(4);
    for (char &byte : mask)
        byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
    frame.append(mask);

    QByteArray maskedPayload = payload;
    for (int i = 0; i < maskedPayload.size(); ++i)
        maskedPayload[i] = maskedPayload[i] ^ mask.at(i % 4);
    frame.append(maskedPayload);
    socket.write(frame);
}

void VOXLMapperClient::sendPlanCommand(const QString &command)
{
    if (!m_planReady) {
        emit errorOccurred(QStringLiteral("VOXL Mapper plan websocket is not connected."));
        return;
    }
    sendWebSocketText(m_planSocket, command);
    emit statusChanged(QStringLiteral("Mapper plan command: %1").arg(command));
}

void VOXLMapperClient::sendMeshCommand(const QString &command)
{
    if (!m_meshReady) {
        emit errorOccurred(QStringLiteral("VOXL Mapper mesh websocket is not connected."));
        return;
    }
    sendWebSocketText(m_meshSocket, command);
    emit statusChanged(QStringLiteral("Mapper mesh command: %1").arg(command));
}

void VOXLMapperClient::handlePoseMessage(const QByteArray &payload)
{
    constexpr int kPosePacketBytes = 84;
    if (payload.size() < kPosePacketBytes)
        return;

    const int offset = payload.size() - kPosePacketBytes;
    const QVector3D position(readFloatLe(payload, offset + 12),
                             readFloatLe(payload, offset + 16),
                             readFloatLe(payload, offset + 20));

    // Sanity check: reject packets with non-finite values or implausibly large
    // positions (> 100 m from origin — VOXL VIO resets if it drifts that far).
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || !std::isfinite(position.z()))
        return;
    if (position.length() > 100.0f)
        return;

    // Reject Z spikes: if Z jumps more than 5 m between consecutive packets
    // it is almost certainly a VIO glitch (visible as huge spikes in telemetry).
    constexpr float kMaxZJumpM = 5.0f;
    if (m_hasLastPose && std::abs(position.z() - m_lastPoseFrd.z()) > kMaxZJumpM)
        return;

    m_lastPoseFrd = position;
    m_hasLastPose = true;

    const float r00 = readFloatLe(payload, offset + 24);
    const float r10 = readFloatLe(payload, offset + 36);
    const float yawRad = std::atan2(r10, r00);

    const QVector3D velocity(readFloatLe(payload, offset + 60),
                             readFloatLe(payload, offset + 64),
                             readFloatLe(payload, offset + 68));

    if (!std::isfinite(velocity.x()) || !std::isfinite(velocity.y()) || !std::isfinite(velocity.z()))
        return;

    emit poseReceived(position, velocity, yawRad);
}

void VOXLMapperClient::handlePlanMessage(const QByteArray &payload)
{
    constexpr int kMetaBytes = 44;
    constexpr int kPointBytes = 15;
    if (payload.size() < kMetaBytes)
        return;

    const quint32 nPoints = readLe<quint32>(payload, 4);
    const quint32 format = readLe<quint32>(payload, 8);
    const QByteArray rawName = payload.mid(12, 32);
    const QString name = QString::fromLatin1(rawName.constData()).trimmed();
    if (nPoints == 0) {
        emit pathRenderReceived(name, static_cast<int>(format), {});
        return;
    }

    const quint64 availablePointBytes = static_cast<quint64>(payload.size() - kMetaBytes);
    const quint64 maxPointsFromPayload = availablePointBytes / static_cast<quint64>(kPointBytes);
    if (static_cast<quint64>(nPoints) > maxPointsFromPayload) {
        qWarning() << "Ignoring VOXL Mapper render packet with invalid point count"
                   << nPoints << "payload bytes" << payload.size();
        return;
    }

    const quint32 step = qMax<quint32>(1, (nPoints + MaxRenderPointsPerPacket - 1) / MaxRenderPointsPerPacket);

    QVector<MapperPathPoint> points;
    points.reserve(static_cast<int>((nPoints + step - 1) / step));
    int cursor = kMetaBytes;
    for (quint32 i = 0; i < nPoints; ++i) {
        if ((i % step) != 0) {
            cursor += kPointBytes;
            continue;
        }

        const float x = readFloatLe(payload, cursor);
        const float y = readFloatLe(payload, cursor + 4);
        const float z = readFloatLe(payload, cursor + 8);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            cursor += kPointBytes;
            continue;
        }

        MapperPathPoint point;
        point.positionFrd = QVector3D(x, y, z);
        point.color = QColor(static_cast<quint8>(payload.at(cursor + 12)),
                             static_cast<quint8>(payload.at(cursor + 13)),
                             static_cast<quint8>(payload.at(cursor + 14)));
        points.append(point);
        cursor += kPointBytes;
    }

    if (!points.isEmpty())
        emit pathRenderReceived(name, static_cast<int>(format), points);
}

void VOXLMapperClient::handleMeshMessage(const QByteArray &payload)
{
    // VOXL Portal web_root/js/mapper.js parses /mesh as:
    // metadata { magic, timestamp_ns[2], size_bytes[2], num_vertices, num_indices }
    // followed by num_vertices * { float x,y,z; uint8 r,g,b }
    // followed by num_indices * { uint32 indices[3] }.
    constexpr int kMetaBytes = 28;
    constexpr int kVertexBytes = 15;
    constexpr int kTriangleIndexBytes = 12;
    if (payload.size() < kMetaBytes)
        return;

    const quint32 numVertices = readLe<quint32>(payload, 20);
    const quint32 numTriangles = readLe<quint32>(payload, 24);
    if (numVertices == 0) {
        emit meshRenderReceived({}, {});
        return;
    }

    if (numVertices > MaxMeshVerticesPerPacket || numTriangles > MaxMeshTrianglesPerPacket) {
        qWarning() << "Ignoring oversized VOXL Mapper mesh packet"
                   << "vertices" << numVertices << "triangles" << numTriangles;
        return;
    }

    const quint64 vertexBytes = static_cast<quint64>(numVertices) * kVertexBytes;
    const quint64 indexBytes = static_cast<quint64>(numTriangles) * kTriangleIndexBytes;
    const quint64 expectedBytes = static_cast<quint64>(kMetaBytes) + vertexBytes + indexBytes;
    if (expectedBytes > static_cast<quint64>(payload.size())) {
        qWarning() << "Ignoring truncated VOXL Mapper mesh packet"
                   << "vertices" << numVertices << "triangles" << numTriangles
                   << "payload bytes" << payload.size();
        return;
    }

    QVector<MapperPathPoint> vertices;
    vertices.reserve(static_cast<int>(numVertices));
    int cursor = kMetaBytes;
    for (quint32 i = 0; i < numVertices; ++i) {
        const float x = readFloatLe(payload, cursor);
        const float y = readFloatLe(payload, cursor + 4);
        const float z = readFloatLe(payload, cursor + 8);
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
            MapperPathPoint point;
            point.positionFrd = QVector3D(x, y, z);
            point.color = QColor(static_cast<quint8>(payload.at(cursor + 12)),
                                 static_cast<quint8>(payload.at(cursor + 13)),
                                 static_cast<quint8>(payload.at(cursor + 14)));
            vertices.append(point);
        } else {
            MapperPathPoint point;
            point.positionFrd = QVector3D();
            point.color = QColor(110, 160, 210);
            vertices.append(point);
        }
        cursor += kVertexBytes;
    }

    QVector<quint32> triangleIndices;
    triangleIndices.reserve(static_cast<int>(numTriangles) * 3);
    for (quint32 i = 0; i < numTriangles; ++i) {
        const quint32 a = readLe<quint32>(payload, cursor);
        const quint32 b = readLe<quint32>(payload, cursor + 4);
        const quint32 c = readLe<quint32>(payload, cursor + 8);
        if (a < numVertices && b < numVertices && c < numVertices)
            triangleIndices << a << b << c;
        cursor += kTriangleIndexBytes;
    }

    emit meshRenderReceived(vertices, triangleIndices);
}

void VOXLMapperClient::updateConnectedState()
{
    const bool nowConnected = isConnected();
    if (m_connected == nowConnected)
        return;
    m_connected = nowConnected;
    emit connectedChanged(m_connected);
}
