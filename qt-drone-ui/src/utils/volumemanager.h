#ifndef VOLUMEMANAGER_H
#define VOLUMEMANAGER_H

#include <QObject>
#include <QDateTime>
#include <QList>
#include <QString>

/// Manages named "volumes" (rooms) — each stores paths and map metadata on disk.
///
///   <app_dir>/volumes/<uuid>/
///       volume.json
///       map/map.json            — remote_path: missions/<room>/
///       paths/                  — saved waypoint JSON files
///       flights/telemetry/
///       flights/trajectories/
class VolumeManager : public QObject
{
    Q_OBJECT

public:
    struct VolumeInfo {
        QString   id;
        QString   name;
        QString   description;
        QDateTime createdAt;
    };

    struct MapInfo {
        QString displayName;
        /// missions/<room>/ subdir used by voxl-mapper load_map / save_map
        QString remotePath;
        QDateTime updatedAt;
    };

    explicit VolumeManager(QObject *parent = nullptr);

    QList<VolumeInfo> volumes() const { return m_volumes; }
    bool hasVolume(const QString &id) const;
    VolumeInfo volumeById(const QString &id) const;

    bool            hasActiveVolume()   const { return !m_activeVolumeId.isEmpty(); }
    VolumeInfo      activeVolume()      const;
    QString         activeVolumeId()    const { return m_activeVolumeId; }

    QString createVolume(const QString &name, const QString &description = QString());
    bool    deleteVolume(const QString &id);
    bool    renameVolume(const QString &id, const QString &newName,
                         const QString &newDescription = QString());
    bool    setActiveVolume(const QString &id);
    void    clearActiveVolume();

    QString volumesRootDir()                          const;
    QString volumeDir            (const QString &id)  const;
    QString mapDir               (const QString &id)  const;
    QString mapMetadataPath      (const QString &id)  const;
    QString flightsTelemetryDir  (const QString &id)  const;
    QString flightsTrajectoriesDir(const QString &id) const;
    QString pathsDir             (const QString &id)  const;

    QString activeVolumeDir()             const;
    QString activeMapDir()                const;
    QString activeMapMetadataPath()       const;
    QString activeFlightsTelemetryDir()   const;
    QString activeTrajectoriesDir()       const;
    QString activePathsDir()              const;

    MapInfo mapInfo(const QString &id) const;
    MapInfo activeMapInfo() const;
    bool writeMapInfo(const QString &id, const MapInfo &info) const;

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
