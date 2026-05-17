#include "volumemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QDebug>

bool VolumeManager::MapInfo::hasBundle() const
{
    return !bundleDir.trimmed().isEmpty() && QDir(bundleDir).exists();
}

bool VolumeManager::MapInfo::hasMesh() const
{
    return !meshPath.trimmed().isEmpty() && QFileInfo::exists(meshPath);
}

bool VolumeManager::MapInfo::isValid() const
{
    return hasBundle() || hasMesh() || !remotePath.trimmed().isEmpty();
}

// ---------------------------------------------------------------------------
// Construction / persistence
// ---------------------------------------------------------------------------

VolumeManager::VolumeManager(QObject *parent)
    : QObject(parent)
{
    loadRegistry();
}

QString VolumeManager::registryPath() const
{
    return volumesRootDir() + QStringLiteral("/volumes.json");
}

void VolumeManager::loadRegistry()
{
    QFile f(registryPath());
    if (!f.open(QIODevice::ReadOnly))
        return; // First run — no registry yet.

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;

    m_volumes.clear();
    for (const QJsonValue &v : doc.object().value(QStringLiteral("volumes")).toArray()) {
        const QJsonObject o = v.toObject();
        VolumeInfo info;
        info.id          = o.value(QStringLiteral("id")).toString();
        info.name        = o.value(QStringLiteral("name")).toString();
        info.description = o.value(QStringLiteral("description")).toString();
        info.createdAt   = QDateTime::fromString(o.value(QStringLiteral("created_at")).toString(),
                                                 Qt::ISODate);
        if (!info.id.isEmpty() && !info.name.isEmpty())
            m_volumes.append(info);
    }

    m_activeVolumeId = doc.object().value(QStringLiteral("active_volume_id")).toString();
    // Validate: active id must still exist in the list.
    if (!hasVolume(m_activeVolumeId))
        m_activeVolumeId.clear();
}

void VolumeManager::saveRegistry() const
{
    QDir().mkpath(volumesRootDir());
    QJsonArray arr;
    for (const VolumeInfo &v : m_volumes) {
        QJsonObject o;
        o[QStringLiteral("id")]          = v.id;
        o[QStringLiteral("name")]        = v.name;
        o[QStringLiteral("description")] = v.description;
        o[QStringLiteral("created_at")]  = v.createdAt.toString(Qt::ISODate);
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("volumes")]          = arr;
    root[QStringLiteral("active_volume_id")] = m_activeVolumeId;

    QFile f(registryPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    else
        qWarning() << "VolumeManager: failed to write registry ->" << registryPath();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool VolumeManager::hasVolume(const QString &id) const
{
    for (const VolumeInfo &v : m_volumes)
        if (v.id == id) return true;
    return false;
}

VolumeManager::VolumeInfo VolumeManager::volumeById(const QString &id) const
{
    for (const VolumeInfo &v : m_volumes)
        if (v.id == id) return v;
    return {};
}

VolumeManager::VolumeInfo VolumeManager::activeVolume() const
{
    return volumeById(m_activeVolumeId);
}

// ---------------------------------------------------------------------------
// CRUD
// ---------------------------------------------------------------------------

QString VolumeManager::createVolume(const QString &name, const QString &description)
{
    if (name.trimmed().isEmpty())
        return {};

    VolumeInfo info;
    info.id          = QUuid::createUuid().toString(QUuid::WithoutBraces);
    info.name        = name.trimmed();
    info.description = description.trimmed();
    info.createdAt   = QDateTime::currentDateTime();

    // Write per-volume metadata file.
    if (!ensureVolumeDirs(info.id)) {
        qWarning() << "VolumeManager: could not create dirs for volume" << info.id;
        return {};
    }
    QJsonObject meta;
    meta[QStringLiteral("id")]          = info.id;
    meta[QStringLiteral("name")]        = info.name;
    meta[QStringLiteral("description")] = info.description;
    meta[QStringLiteral("created_at")]  = info.createdAt.toString(Qt::ISODate);
    QFile mf(volumeDir(info.id) + QStringLiteral("/volume.json"));
    if (mf.open(QIODevice::WriteOnly | QIODevice::Text))
        mf.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));

    m_volumes.append(info);
    saveRegistry();
    emit volumeListChanged();
    return info.id;
}

bool VolumeManager::deleteVolume(const QString &id)
{
    const int idx = [&]() {
        for (int i = 0; i < m_volumes.size(); ++i)
            if (m_volumes[i].id == id) return i;
        return -1;
    }();
    if (idx < 0)
        return false;

    // Remove from list and registry (does NOT delete files — caller decides).
    m_volumes.removeAt(idx);
    if (m_activeVolumeId == id) {
        m_activeVolumeId.clear();
        emit activeVolumeCleared();
    }
    saveRegistry();
    emit volumeListChanged();
    return true;
}

bool VolumeManager::renameVolume(const QString &id, const QString &newName,
                                  const QString &newDescription)
{
    for (VolumeInfo &v : m_volumes) {
        if (v.id == id) {
            v.name        = newName.trimmed();
            v.description = newDescription.trimmed();

            // Update per-volume metadata.
            QJsonObject meta;
            meta[QStringLiteral("id")]          = v.id;
            meta[QStringLiteral("name")]        = v.name;
            meta[QStringLiteral("description")] = v.description;
            meta[QStringLiteral("created_at")]  = v.createdAt.toString(Qt::ISODate);
            QFile mf(volumeDir(id) + QStringLiteral("/volume.json"));
            if (mf.open(QIODevice::WriteOnly | QIODevice::Text))
                mf.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));

            saveRegistry();
            if (id == m_activeVolumeId)
                emit activeVolumeChanged(v);
            emit volumeListChanged();
            return true;
        }
    }
    return false;
}

bool VolumeManager::setActiveVolume(const QString &id)
{
    if (!hasVolume(id))
        return false;
    m_activeVolumeId = id;
    saveRegistry();
    emit activeVolumeChanged(volumeById(id));
    return true;
}

void VolumeManager::clearActiveVolume()
{
    if (m_activeVolumeId.isEmpty())
        return;
    m_activeVolumeId.clear();
    saveRegistry();
    emit activeVolumeCleared();
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

QString VolumeManager::volumesRootDir() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/volumes");
}

QString VolumeManager::volumeDir(const QString &id) const
{
    return volumesRootDir() + QStringLiteral("/") + id;
}

QString VolumeManager::mapDir(const QString &id) const
{
    return volumeDir(id) + QStringLiteral("/map");
}

QString VolumeManager::mapMetadataPath(const QString &id) const
{
    return mapDir(id) + QStringLiteral("/map.json");
}

QString VolumeManager::mapBundleDir(const QString &id) const
{
    return mapDir(id) + QStringLiteral("/mapper_map");
}

QString VolumeManager::mapMeshPath(const QString &id) const
{
    return mapDir(id) + QStringLiteral("/map.ply");
}

QString VolumeManager::flightsTelemetryDir(const QString &id) const
{
    return volumeDir(id) + QStringLiteral("/flights/telemetry");
}

QString VolumeManager::flightsTrajectoriesDir(const QString &id) const
{
    return volumeDir(id) + QStringLiteral("/flights/trajectories");
}

QString VolumeManager::pathsDir(const QString &id) const
{
    return volumeDir(id) + QStringLiteral("/paths");
}

bool VolumeManager::ensureVolumeDirs(const QString &id) const
{
    return QDir().mkpath(mapDir(id))
        && QDir().mkpath(flightsTelemetryDir(id))
        && QDir().mkpath(flightsTrajectoriesDir(id))
        && QDir().mkpath(pathsDir(id));
}

// Active-volume shortcuts
QString VolumeManager::activeVolumeDir()           const { return hasActiveVolume() ? volumeDir(m_activeVolumeId)               : QString(); }
QString VolumeManager::activeMapDir()              const { return hasActiveVolume() ? mapDir(m_activeVolumeId)                   : QString(); }
QString VolumeManager::activeMapMetadataPath()     const { return hasActiveVolume() ? mapMetadataPath(m_activeVolumeId)          : QString(); }
QString VolumeManager::activeMapBundleDir()        const { return hasActiveVolume() ? mapBundleDir(m_activeVolumeId)             : QString(); }
QString VolumeManager::activeMapMeshPath()         const { return hasActiveVolume() ? mapMeshPath(m_activeVolumeId)              : QString(); }
QString VolumeManager::activeFlightsTelemetryDir() const { return hasActiveVolume() ? flightsTelemetryDir(m_activeVolumeId)      : QString(); }
QString VolumeManager::activeTrajectoriesDir()     const { return hasActiveVolume() ? flightsTrajectoriesDir(m_activeVolumeId)   : QString(); }
QString VolumeManager::activePathsDir()            const { return hasActiveVolume() ? pathsDir(m_activeVolumeId)                 : QString(); }

VolumeManager::MapInfo VolumeManager::mapInfo(const QString &id) const
{
    MapInfo info;
    if (!hasVolume(id))
        return info;

    info.displayName = volumeById(id).name;
    info.bundleDir = QDir::toNativeSeparators(mapBundleDir(id));
    info.meshPath = QDir::toNativeSeparators(mapMeshPath(id));

    QFile f(mapMetadataPath(id));
    if (!f.open(QIODevice::ReadOnly))
        return info;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return info;

    const QJsonObject root = doc.object();
    const QString displayName = root.value(QStringLiteral("display_name")).toString();
    if (!displayName.trimmed().isEmpty())
        info.displayName = displayName.trimmed();
    info.remotePath = root.value(QStringLiteral("remote_path")).toString();
    const QString bundleDirValue = root.value(QStringLiteral("bundle_dir")).toString();
    if (!bundleDirValue.trimmed().isEmpty())
        info.bundleDir = QDir(mapDir(id)).filePath(bundleDirValue.trimmed());
    const QString meshPathValue = root.value(QStringLiteral("mesh_path")).toString();
    if (!meshPathValue.trimmed().isEmpty())
        info.meshPath = QDir(mapDir(id)).filePath(meshPathValue.trimmed());
    info.bundleDir = QDir::toNativeSeparators(info.bundleDir);
    info.meshPath = QDir::toNativeSeparators(info.meshPath);
    info.updatedAt = QDateTime::fromString(root.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
    return info;
}

VolumeManager::MapInfo VolumeManager::activeMapInfo() const
{
    return hasActiveVolume() ? mapInfo(m_activeVolumeId) : MapInfo();
}

bool VolumeManager::writeMapInfo(const QString &id, const MapInfo &info) const
{
    if (!hasVolume(id))
        return false;
    if (!QDir().mkpath(mapDir(id)))
        return false;

    QJsonObject root;
    root[QStringLiteral("display_name")] = info.displayName.trimmed().isEmpty()
                                               ? volumeById(id).name
                                               : info.displayName.trimmed();
    root[QStringLiteral("remote_path")] = info.remotePath.trimmed();
    root[QStringLiteral("bundle_dir")] = QStringLiteral("mapper_map");
    root[QStringLiteral("mesh_path")] = QStringLiteral("map.ply");
    root[QStringLiteral("updated_at")] = (info.updatedAt.isValid() ? info.updatedAt : QDateTime::currentDateTime()).toString(Qt::ISODate);

    QFile f(mapMetadataPath(id));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "VolumeManager: failed to write map metadata ->" << mapMetadataPath(id);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
