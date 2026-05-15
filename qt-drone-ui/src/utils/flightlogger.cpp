#include "flightlogger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

#include "../models/flightpath.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FlightLogger::FlightLogger(QObject *parent)
    : QObject(parent)
    , m_trajTimer(new QTimer(this))
    , m_lastYawDeg(0.0f)
    , m_havePose(false)
{
    m_csvStream.setDevice(&m_csvFile);

    // 2 Hz trajectory sampler.
    m_trajTimer->setInterval(500);
    m_trajTimer->setSingleShot(false);
    connect(m_trajTimer, &QTimer::timeout, this, &FlightLogger::onTrajectoryTick);
}

FlightLogger::~FlightLogger()
{
    endSession();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void FlightLogger::setVolumeFlightDirs(const QString &telemetryDir,
                                        const QString &trajectoriesDir)
{
    m_telemetryDir    = telemetryDir;
    m_trajectoriesDir = trajectoriesDir;
}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

void FlightLogger::startSession()
{
    endSession(); // Close any previously open files.

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));

    // Resolve output directories — fall back to <app_dir>/logs/ when no volume.
    const QString fallbackDir = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    const QString csvDir  = m_telemetryDir.isEmpty()    ? fallbackDir : m_telemetryDir;
    const QString trajDir = m_trajectoriesDir.isEmpty() ? fallbackDir : m_trajectoriesDir;
    QDir().mkpath(csvDir);
    QDir().mkpath(trajDir);

    // --- Telemetry CSV ---
    const QString csvPath = csvDir + QStringLiteral("/flight_%1.csv").arg(timestamp);
    m_csvFile.setFileName(csvPath);
    if (!m_csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "FlightLogger: cannot open telemetry file:" << csvPath;
    } else {
        writeCsvHeader();
        qDebug() << "FlightLogger: telemetry ->" << csvPath;
    }

    // --- Trajectory JSON ---
    m_trajFilePath = trajDir + QStringLiteral("/traj_%1.json").arg(timestamp);
    m_trajWaypoints.clear();
    m_havePose = false;

    m_sessionTimer.restart();
    m_trajTimer->start();
    qDebug() << "FlightLogger: trajectory ->" << m_trajFilePath;
}

void FlightLogger::endSession()
{
    m_trajTimer->stop();

    if (m_csvFile.isOpen()) {
        m_csvStream.flush();
        m_csvFile.close();
        qDebug() << "FlightLogger: telemetry closed ->" << m_csvFile.fileName();
    }

    finaliseTrajectoryFile();
}

// ---------------------------------------------------------------------------
// 10 Hz telemetry CSV
// ---------------------------------------------------------------------------

void FlightLogger::writeCsvHeader()
{
    m_csvStream
        << "elapsed_ms"
        << ",wall_time_iso"
        << ",pos_x_m"           // planner logical: X forward
        << ",pos_y_m"           // planner logical: Y left
        << ",pos_z_m"           // planner logical: Z up
        << ",vel_x_ms"
        << ",vel_y_ms"
        << ",vel_z_ms"
        << ",roll_deg"
        << ",pitch_deg"
        << ",yaw_deg"
        << ",altitude_m"
        << ",ground_speed_ms"
        << ",vertical_speed_ms"
        << ",flight_mode"
        << ",armed"
        << ",battery_pct"
        << ",battery_v"
        << ",pos_is_mapper_local"
        << ",system_status"
        << "\n";
    m_csvStream.flush();
}

void FlightLogger::logStatus(const DroneStatus &status)
{
    if (!m_csvFile.isOpen())
        return;

    // Only log local mapper position when the flag confirms it is mapper-local.
    // When positionIsMapperLocal == false the position field holds GPS lat/lon/alt
    // which must NOT appear in the local-coordinate columns.
    const bool hasLocalPos = status.positionIsMapperLocal;

    m_csvStream
        << m_sessionTimer.elapsed()
        << "," << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        << "," << (hasLocalPos ? QString::number(static_cast<double>(status.position.x())) : QStringLiteral("nan"))
        << "," << (hasLocalPos ? QString::number(static_cast<double>(status.position.y())) : QStringLiteral("nan"))
        << "," << (hasLocalPos ? QString::number(static_cast<double>(status.position.z())) : QStringLiteral("nan"))
        << "," << status.velocity.x()
        << "," << status.velocity.y()
        << "," << status.velocity.z()
        << "," << status.attitude.x()   // roll
        << "," << status.attitude.y()   // pitch
        << "," << status.attitude.z()   // yaw
        << "," << status.altitude
        << "," << status.groundSpeed
        << "," << status.verticalSpeed
        << "," << QString(status.flightMode).replace(QLatin1Char(','), QLatin1Char(';'))
        << "," << (status.armed ? 1 : 0)
        << "," << status.batteryPercentage
        << "," << status.batteryVoltage
        << "," << (status.positionIsMapperLocal ? 1 : 0)
        << "," << QString(status.systemStatus).replace(QLatin1Char(','), QLatin1Char(';'))
        << "\n";

    // Flush every row so data is safe on an unexpected disconnect or crash.
    m_csvStream.flush();
}

// ---------------------------------------------------------------------------
// 2 Hz trajectory recorder
// ---------------------------------------------------------------------------

void FlightLogger::onPoseUpdated(const QVector3D &positionLogical, float yawDeg)
{
    m_lastPoseLogical = positionLogical;
    m_lastYawDeg      = yawDeg;
    m_havePose        = true;
}

void FlightLogger::onTrajectoryTick()
{
    if (!m_havePose || m_trajFilePath.isEmpty())
        return;

    // Build a Waypoint from the latest pose.  Acceptance radius is set tight
    // (0.15 m) so replays follow the path closely; hold time is 0.
    Waypoint wp(m_lastPoseLogical);
    wp.setYawAngle(m_lastYawDeg);
    wp.setAcceptanceRadius(0.15f);
    wp.setHoldTime(0.0f);
    wp.setSequence(static_cast<int>(m_trajWaypoints.size()));
    wp.setName(QStringLiteral("rec_%1ms").arg(m_sessionTimer.elapsed()));
    m_trajWaypoints.append(wp);
}

void FlightLogger::finaliseTrajectoryFile()
{
    if (m_trajFilePath.isEmpty() || m_trajWaypoints.isEmpty())
        return;

    // Build a FlightPath and serialise it to the standard planner JSON format.
    FlightPath path(QStringLiteral("Recorded trajectory %1")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    path.setDescription(QStringLiteral("Auto-recorded at 2 Hz. Load in the path planner to replay."));
    path.setCruiseSpeedMS(1.5f);    // Conservative replay speed
    path.setHomeRelative(true);
    path.setAutoLand(false);        // Operator decides when to land during replay

    for (const Waypoint &wp : m_trajWaypoints)
        path.addWaypoint(wp);

    QJsonObject root;
    root[QStringLiteral("version")]    = 1;
    root[QStringLiteral("name")]       = path.name();
    root[QStringLiteral("created_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root[QStringLiteral("modified_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray waypointsArray;
    for (const Waypoint &wp : m_trajWaypoints)
        waypointsArray.append(wp.toJson());
    root[QStringLiteral("waypoints")] = waypointsArray;

    // Add basic FlightPath mission parameters so the planner can use them.
    root[QStringLiteral("home_relative")]     = path.homeRelative();
    root[QStringLiteral("takeoff_alt_m")]     = static_cast<double>(path.takeoffAltM());
    root[QStringLiteral("cruise_speed_m_s")]  = static_cast<double>(path.cruiseSpeedMS());
    root[QStringLiteral("land")]              = path.autoLand();

    QFile f(m_trajFilePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        qDebug() << "FlightLogger: trajectory saved ->" << m_trajFilePath
                 << "(" << m_trajWaypoints.size() << "waypoints )";
    } else {
        qWarning() << "FlightLogger: cannot write trajectory file:" << m_trajFilePath;
    }

    m_trajWaypoints.clear();
    m_trajFilePath.clear();
}

