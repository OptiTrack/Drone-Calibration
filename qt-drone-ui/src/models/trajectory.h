#ifndef TRAJECTORY_H
#define TRAJECTORY_H

#include "waypoint.h"
#include <QVector>
#include <QVector3D>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

struct TrajectorySample {
    double t;               // seconds from start
    QVector3D positionNed;  // North, East, Down (meters)
    QVector3D velocityNed;  // m/s
    float yawRad;

    QJsonObject toJson() const;
};

class Trajectory
{
public:
    Trajectory();

    // Densify a list of logical-frame waypoints (Z-up) into time-sampled NED samples.
    // cruiseSpeedMs > 0, sampleHz > 0. Final sample velocity is forced to zero.
    // Yaw: if the source waypoint has a non-zero yawAngle(), use it (converted to rad);
    // otherwise yaw follows the velocity heading. For the final (zero-vel) sample, hold the previous yaw.
    // Coordinate flip: NED = (logical.x, -logical.y, -logical.z)
    static Trajectory fromWaypoints(const std::vector<Waypoint>& waypointsLogical,
                                    float cruiseSpeedMs,
                                    float sampleHz,
                                    const QString& name = QString());

    // Returns empty list if valid; otherwise human-readable rule violations.
    // Rules: |north|<=50, |east|<=50, -30<=down<=0, |v|<=5 m/s,
    // monotonically increasing t, final |v| < 1e-3.
    QStringList validate() const;

    // { name, hz, cruise_ms, samples: [{t, p:[n,e,d], v:[n,e,d], yaw}, ...] }
    QJsonObject toJson() const;

    float duration() const;     // seconds
    float distance() const;     // total path length (sum of |Δp|), meters
    float peakSpeedMs() const;  // max |v|

    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }
    float cruiseSpeedMs() const { return m_cruiseSpeedMs; }
    float sampleHz() const { return m_sampleHz; }
    const QVector<TrajectorySample>& samples() const { return m_samples; }
    bool isEmpty() const { return m_samples.isEmpty(); }

    // Position-only preview for visualization. Returns the sampled NED positions
    // from a Trajectory built with fromWaypoints, without exposing the full sample set.
    static QVector<QVector3D> previewSampledPositionsNed(
        const std::vector<Waypoint>& waypointsLogical,
        float cruiseSpeedMs,
        float sampleHz);

    // Same as previewSampledPositionsNed but in the logical (Z-up) frame used by the
    // renderer. Each NED position (n, e, d) is mapped back to (n, -e, -d).
    static QVector<QVector3D> previewSampledPositionsLogical(
        const std::vector<Waypoint>& waypointsLogical,
        float cruiseSpeedMs,
        float sampleHz);

private:
    QString m_name;
    float m_cruiseSpeedMs = 0.0f;
    float m_sampleHz = 0.0f;
    QVector<TrajectorySample> m_samples;
};

#endif // TRAJECTORY_H
