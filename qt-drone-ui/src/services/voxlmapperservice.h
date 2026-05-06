#ifndef VOXLMAPPERSERVICE_H
#define VOXLMAPPERSERVICE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>

/**
 * VoxlMapperService
 *
 * Provides the Qt-side interface to voxl-mapper running on the VOXL 2.
 *
 * Communication strategy:
 *   - Mesh download  : HTTP GET  http://{host}/mesh_api/{format}   (VOXL Portal port 80)
 *   - Map control    : SSH pipe write to /run/mpa/voxl_mesh/control
 *     (matches the CONTROL_COMMANDS handled in voxl_mapper.cc)
 *   - Path planning  : SSH pipe write to /run/mpa/voxl_planner/control
 *     (reset_vio, save_map, load_map, clear_map, slice_level,
 *      plan_to, follow_path, abort, clear_paths are the supported commands)
 *
 * Note: SLAM / VIO position data arrives via the existing MAVLink
 * LOCAL_POSITION_NED stream (msg 32) – no extra channel needed.
 */
class VoxlMapperService : public QObject
{
    Q_OBJECT

public:
    enum MeshFormat {
        PLY,
        OBJ,
        GLTF
    };
    Q_ENUM(MeshFormat)

    explicit VoxlMapperService(QObject *parent = nullptr);
    ~VoxlMapperService();

    // ── Configuration ──────────────────────────────────────────────────────
    void setHost(const QString &host);
    QString host() const { return m_host; }

    /// Port that VOXL Portal / the web server listens on (default 80).
    void setPortalPort(int port) { m_portalPort = port; }
    int portalPort() const { return m_portalPort; }

    /// SSH root password for the VOXL 2 (needed for pipe-write control).
    void setSshPassword(const QString &password) { m_sshPassword = password; }

    // ── Mesh download (HTTP) ───────────────────────────────────────────────
    /**
     * Download the current 3-D mesh from voxl-mapper.
     * On success emits meshReceived(data, format).
     * On failure emits meshFetchFailed(error).
     */
    void fetchMesh(MeshFormat format = PLY);

    // ── Map-level control (SSH → MPA pipe) ────────────────────────────────
    /**
     * Tell voxl-mapper to save the current map to disk.
     * @param dir   Sub-directory under /data/voxl-mapper/ (empty = default)
     * @param format Mesh file format to save alongside the ESDF/TSDF maps
     */
    void saveMap(const QString &dir = "", MeshFormat format = PLY);
    void loadMap(const QString &dir = "");
    void clearMap();
    void resetVio();

    /**
     * Set the height (0-100 %) at which the 2-D costmap slice is computed.
     * 0 = bottom of map, 100 = top.
     */
    void setSliceLevel(double level);
    void setMeshColorMap(const QString &colorMap);
    void setMeshColorMode(const QString &colorMode);

    // ── Path planning (SSH → MPA pipe) ────────────────────────────────────
    /**
     * Request an RRT* plan from the drone's current position to the given
     * local NED coordinates (same frame as LOCAL_POSITION_NED).
     */
    void planToPoint(float x, float y, float z);

    /**
     * Plan to the home position (0, 0, -1.5 m above takeoff spot).
     */
    void planToHome();

    /**
     * Start following the most recently generated plan.
     */
    void followPath();

    /**
     * Abort the current path following immediately.
     */
    void abortPath();

    /**
     * Clear any displayed path remnants without clearing the map.
     */
    void clearPaths();

signals:
    // Mesh download
    void meshReceived(const QByteArray &data, VoxlMapperService::MeshFormat format);
    void meshFetchFailed(const QString &error);

    // General control feedback
    void commandSent(const QString &command);
    void commandFailed(const QString &command, const QString &error);
    void statusChanged(const QString &status);

private slots:
    void onMeshReplyFinished();
    void onSshProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onSshProcessError(QProcess::ProcessError error);

private:
    void sendPipeCommand(const QString &pipe, const QString &command);
    void runSshCommand(const QString &remoteCmd, const QString &commandLabel);
    QString meshFormatString(MeshFormat format) const;

    QString m_host;
    int m_portalPort = 80;
    QString m_sshPassword;

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_meshReply = nullptr;
    MeshFormat m_pendingMeshFormat = PLY;

    // SSH process queue: one at a time
    QProcess *m_sshProcess = nullptr;
    struct SshCmd { QString remoteCmd; QString label; };
    QList<SshCmd> m_sshQueue;
    bool m_sshBusy = false;

    void dispatchNextSsh();
};

#endif // VOXLMAPPERSERVICE_H
