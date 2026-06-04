#ifndef VOXLMAPPATHS_H
#define VOXLMAPPATHS_H

#include <QString>

/// Helpers for voxl-mapper map I/O path conventions.
///
/// save_map / load_map "file:" arguments are relative subdirectory names under
/// /data/voxl-mapper/ on the drone (e.g. missions/MyRoom/). They are NOT absolute paths.
class VoxlMapperPaths
{
public:
    /// WebSocket file: argument, e.g. "missions/MyRoom/" (trailing slash required by voxl-mapper).
    static QString roomSubdir(const QString &roomBaseName);

    /// Normalize to missions/.../ form; returns empty if not a missions path.
    static QString normalizeSubdir(const QString &storedPath);

    /// One SSH round-trip: mission folder names (ROOM:<name>), then MAP_READY or MAP_WAIT for @a customSubdir.
    static QString listRoomsAndPollMapScript(const QString &customSubdir);

    /// Shell script: one mission folder name per line under the VOXL mapper missions directory.
    static QString listMissionRoomsScript();

};

#endif // VOXLMAPPATHS_H
