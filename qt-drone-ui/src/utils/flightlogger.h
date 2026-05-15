#ifndef FLIGHTLOGGER_H
#define FLIGHTLOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector3D>
#include <QVector>
#include "../widgets/dronestatuswidget.h"
#include "../models/waypoint.h"

/// Writes both a 10 Hz telemetry CSV and a 2 Hz trajectory JSON per flight session.
///
/// Outputs go to the active volume's directory tree when one is set:
///   telemetry  -> <telemetryDir>/flight_YYYYMMDD_HHmmss.csv
///   trajectory -> <trajectoriesDir>/traj_YYYYMMDD_HHmmss.json
///
/// When no volume is configured the files land in <app_dir>/logs/.
///
/// The trajectory JSON uses the standard FlightPath waypoint format so it can be
/// loaded directly into the path planner and replayed autonomously.
class FlightLogger : public QObject
{
    Q_OBJECT

public:
    explicit FlightLogger(QObject *parent = nullptr);
    ~FlightLogger();

    /// Override output directories before or during a session.
    /// Pass empty strings to fall back to <app_dir>/logs/.
    /// Takes effect on the next startSession() call.
    void setVolumeFlightDirs(const QString &telemetryDir, const QString &trajectoriesDir);

    /// Open both log files and start the 2 Hz trajectory timer.
    /// Safe to call while already logging — closes the previous session first.
    void startSession();

    /// Flush, finalise, and close both log files.
    void endSession();

    bool    isLogging()          const { return m_csvFile.isOpen(); }
    QString telemetryFilePath()  const { return m_csvFile.fileName(); }
    QString trajectoryFilePath() const { return m_trajFilePath; }

public slots:
    /// Connected to DroneController::statusUpdated — appends one CSV row at ~10 Hz.
    void logStatus(const DroneStatus &status);

    /// Connected to DroneController::mapperPoseUpdated — stores the latest pose
    /// so the 2 Hz trajectory sampler can record it without writing on every tick.
    void onPoseUpdated(const QVector3D &positionLogical, float yawDeg);

private slots:
    /// Fires every 500 ms (2 Hz) and appends one waypoint to the trajectory buffer.
    void onTrajectoryTick();

private:
    void writeCsvHeader();
    void finaliseTrajectoryFile();

    // 10 Hz telemetry CSV
    QFile       m_csvFile;
    QTextStream m_csvStream;

    // 2 Hz trajectory (buffered in memory, written as FlightPath JSON at endSession)
    QString           m_trajFilePath;
    QVector<Waypoint> m_trajWaypoints;
    QTimer           *m_trajTimer;
    QVector3D         m_lastPoseLogical;
    float             m_lastYawDeg;
    bool              m_havePose;

    // Configurable output directories (empty = use default logs/ dir)
    QString m_telemetryDir;
    QString m_trajectoriesDir;

    QElapsedTimer m_sessionTimer;
};

#endif // FLIGHTLOGGER_H
