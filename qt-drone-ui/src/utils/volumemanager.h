#ifndef VOLUMEMANAGER_H
#define VOLUMEMANAGER_H

#include <QObject>
#include <QDateTime>
#include <QList>
#include <QString>

/// Manages named "volumes" — physical spaces where the drone is operated.
///
/// Each volume owns its own directory tree:
///   <app_dir>/volumes/<uuid>/
///       volume.json               — name, description, created_at
///       map/                      — VOXL Mapper SLAM bundle (.ply + bundle folder)
///       flights/telemetry/        — 10 Hz CSV telemetry files
///       flights/trajectories/     — 2 Hz trajectory JSON files (FlightPath format, replayable)
///       paths/                    — saved waypoint mission files
///
/// A master registry at <app_dir>/volumes/volumes.json enumerates all volumes.
class VolumeManager : public QObject
{
    Q_OBJECT

public:
    struct VolumeInfo {
        QString   id;           ///< UUID string
        QString   name;         ///< Human-readable label ("Lab Room 2", "Arena B")
        QString   description;
        QDateTime createdAt;
    };

    struct MapInfo {
        QString displayName;
        QString remotePath;
        QString bundleDir;
        QString meshPath;
        QDateTime updatedAt;

        bool hasBundle() const;
        bool hasMesh() const;
        bool isValid() const;
    };

    explicit VolumeManager(QObject *parent = nullptr);

    // Volume list
    QList<VolumeInfo> volumes() const { return m_volumes; }
    bool hasVolume(const QString &id) const;
    VolumeInfo volumeById(const QString &id) const;

    // Active volume
    bool            hasActiveVolume()   const { return !m_activeVolumeId.isEmpty(); }
    VolumeInfo      activeVolume()      const;
    QString         activeVolumeId()    const { return m_activeVolumeId; }

    // CRUD
    /// Create a new volume and return its id.  Returns empty string on failure.
    QString createVolume(const QString &name, const QString &description = QString());
    bool    deleteVolume(const QString &id);
    bool    renameVolume(const QString &id, const QString &newName,
                         const QString &newDescription = QString());
    bool    setActiveVolume(const QString &id);
    void    clearActiveVolume();

    // Path helpers (absolute, ready to use with QDir/QFile)
    QString volumesRootDir()                          const;
    QString volumeDir            (const QString &id)  const;
    QString mapDir               (const QString &id)  const;
    QString mapMetadataPath      (const QString &id)  const;
    QString mapBundleDir         (const QString &id)  const;
    QString mapMeshPath          (const QString &id)  const;
    QString flightsTelemetryDir  (const QString &id)  const;
    QString flightsTrajectoriesDir(const QString &id) const;
    QString pathsDir             (const QString &id)  const;

    // Shortcuts for the currently active volume (return "" when none is set)
    QString activeVolumeDir()             const;
    QString activeMapDir()                const;
    QString activeMapMetadataPath()       const;
    QString activeMapBundleDir()          const;
    QString activeMapMeshPath()           const;
    QString activeFlightsTelemetryDir()   const;
    QString activeTrajectoriesDir()       const;
    QString activePathsDir()              const;

    MapInfo mapInfo(const QString &id) const;
    MapInfo activeMapInfo() const;
    bool writeMapInfo(const QString &id, const MapInfo &info) const;

    /// Ensure all sub-directories for a volume exist on disk.
    bool ensureVolumeDirs(const QString &id) const;

signals:
    void volumeListChanged();
    void activeVolumeChanged(const VolumeManager::VolumeInfo &volume);
    void activeVolumeCleared();

private:
    void   loadRegistry();
    void   saveRegistry() const;
    QString registryPath() const;

    QList<VolumeInfo> m_volumes;
    QString           m_activeVolumeId;
};

#endif // VOLUMEMANAGER_H
