#ifndef VOXLMAPPERCLIENT_H
#define VOXLMAPPERCLIENT_H

#include <QObject>
#include <QColor>
#include <QTcpSocket>
#include <QTimer>
#include <QVector3D>
#include <QVector>

struct MapperPathPoint
{
    QVector3D positionFrd;
    QColor color;
};

class VOXLMapperClient : public QObject
{
    Q_OBJECT

public:
    explicit VOXLMapperClient(QObject *parent = nullptr);

    void connectToMapper(const QString &host, int port = 80);
    void disconnectFromMapper();
    /// Cycle only the /mesh WebSocket without touching /plan or /pose.
    /// Call after clear_map so we hold a fresh socket for future commands.
    void reconnectMesh();
    /// Cycle only the /pose WebSocket. Called automatically on disconnect;
    /// can also be called manually to recover a dropped pose stream.
    void reconnectPose();
    bool isConnected() const;
    bool isPlanConnected() const { return m_planReady; }
    bool isMeshConnected() const { return m_meshReady; }
    bool isPoseConnected() const { return m_poseReady; }

    void planToFrd(const QVector3D &targetFrd);
    void planHome();
    void followPath();
    void stopFollowing();

    void clearMap();
    void resetVio();
    void loadMap(const QString &remotePath = QString());
    void saveMap(const QString &format = QStringLiteral("ply"), const QString &remotePath = QString());
    void setCostmapSlice(float sliceLevel);

signals:
    void connectedChanged(bool connected);
    void meshConnectedChanged(bool connected);
    void poseReceived(const QVector3D &positionFrd, const QVector3D &velocityFrd, float yawRad);
    void pathRenderReceived(const QString &name, int format, const QVector<MapperPathPoint> &points);
    void meshRenderReceived(const QVector<MapperPathPoint> &vertices, const QVector<quint32> &triangleIndices);
    void errorOccurred(const QString &error);
    void statusChanged(const QString &status);

private:
    void openSocket(QTcpSocket &socket, const QString &path);
    void sendHandshake(QTcpSocket &socket, const QString &path);
    void handleSocketData(QTcpSocket &socket, QByteArray &buffer, bool &ready, const QString &name);
    void sendWebSocketText(QTcpSocket &socket, const QString &command);
    void sendPlanCommand(const QString &command);
    void sendMeshCommand(const QString &command);
    void handlePoseMessage(const QByteArray &payload);
    void handlePlanMessage(const QByteArray &payload);
    void handleMeshMessage(const QByteArray &payload);
    void updateConnectedState();

    QString m_host;
    int m_port;
    QTcpSocket m_planSocket;
    QTcpSocket m_meshSocket;
    QTcpSocket m_poseSocket;
    QByteArray m_planBuffer;
    QByteArray m_meshBuffer;
    QByteArray m_poseBuffer;
    bool m_planReady;
    bool m_meshReady;
    bool m_poseReady;
    bool m_connected;
    QTimer *m_poseReconnectTimer;
    bool m_hasLastPose;
    QVector3D m_lastPoseFrd;
};

#endif // VOXLMAPPERCLIENT_H
