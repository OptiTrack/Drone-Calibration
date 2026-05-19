#include "trajectory.h"
#include <QJsonArray>
#include <QtMath>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// TrajectorySample
// ---------------------------------------------------------------------------

QJsonObject TrajectorySample::toJson() const
{
    QJsonObject obj;
    obj["t"] = t;
    obj["p"] = QJsonArray{ positionNed.x(), positionNed.y(), positionNed.z() };
    obj["v"] = QJsonArray{ velocityNed.x(), velocityNed.y(), velocityNed.z() };
    obj["yaw"] = static_cast<double>(yawRad);
    return obj;
}

// ---------------------------------------------------------------------------
// Trajectory
// ---------------------------------------------------------------------------

Trajectory::Trajectory() = default;

// Centripetal Catmull-Rom evaluation (Barry-Goldman form, alpha = 0.5).
// Returns position on the segment between P1 and P2 at parameter tc in [t1, t2].
static QVector3D catmullRomEval(const QVector3D& P0, const QVector3D& P1,
                                const QVector3D& P2, const QVector3D& P3,
                                float t0, float t1, float t2, float t3, float tc)
{
    const float d10 = std::max(t1 - t0, 1e-6f);
    const float d21 = std::max(t2 - t1, 1e-6f);
    const float d32 = std::max(t3 - t2, 1e-6f);
    const float d20 = std::max(t2 - t0, 1e-6f);
    const float d31 = std::max(t3 - t1, 1e-6f);

    QVector3D A1 = ((t1 - tc) / d10) * P0 + ((tc - t0) / d10) * P1;
    QVector3D A2 = ((t2 - tc) / d21) * P1 + ((tc - t1) / d21) * P2;
    QVector3D A3 = ((t3 - tc) / d32) * P2 + ((tc - t2) / d32) * P3;
    QVector3D B1 = ((t2 - tc) / d20) * A1 + ((tc - t0) / d20) * A2;
    QVector3D B2 = ((t3 - tc) / d31) * A2 + ((tc - t1) / d31) * A3;
    return ((t2 - tc) / d21) * B1 + ((tc - t1) / d21) * B2;
}

Trajectory Trajectory::fromWaypoints(const std::vector<Waypoint>& waypointsLogical,
                                     float cruiseSpeedMs,
                                     float sampleHz,
                                     const QString& name)
{
    Trajectory traj;
    traj.m_name = name;
    traj.m_cruiseSpeedMs = cruiseSpeedMs;
    traj.m_sampleHz = sampleHz;

    if (cruiseSpeedMs <= 0.0f || sampleHz <= 0.0f)
        return traj;

    // Skip mapper-home entries.
    std::vector<Waypoint> wps;
    wps.reserve(waypointsLogical.size());
    for (const Waypoint& wp : waypointsLogical) {
        if (!wp.isMapperHome())
            wps.push_back(wp);
    }
    if (wps.size() < 2)
        return traj;

    // Convert to NED positions: (logical.x, -logical.y, -logical.z).
    const int M = static_cast<int>(wps.size());
    QVector<QVector3D> P(M);
    for (int i = 0; i < M; ++i) {
        const QVector3D& p = wps[i].position();
        P[i] = QVector3D(p.x(), -p.y(), -p.z());
    }

    const float dt = 1.0f / sampleHz;
    const float ds = cruiseSpeedMs * dt; // step in meters between samples
    if (ds <= 1e-6f)
        return traj;

    struct Raw {
        QVector3D posNed;
        int   srcLeg;        // index of leg-start waypoint (for yaw lookup)
        bool  curveSample;   // true => yaw must be auto (atan2 of velocity)
    };
    QVector<Raw> raw;
    raw.reserve(256);

    // For each leg in (P[seg], P[seg+1]) emit samples [0..nSteps); endpoint emitted once at the end.
    for (int seg = 0; seg < M - 1; ++seg) {
        const QVector3D& A = P[seg];
        const QVector3D& B = P[seg + 1];
        const bool bothCurve = wps[seg].isCurve() && wps[seg + 1].isCurve();

        // -- Curve leg: centripetal Catmull-Rom (alpha = 0.5) --
        if (bothCurve) {
            const QVector3D P0 = (seg == 0)     ? (2.0f * A - B) : P[seg - 1];
            const QVector3D P3 = (seg == M - 2) ? (2.0f * B - A) : P[seg + 2];
            const float t0 = 0.0f;
            const float t1 = t0 + std::sqrt((A - P0).length());
            const float t2 = t1 + std::sqrt((B - A).length());
            const float t3 = t2 + std::sqrt((P3 - B).length());

            if ((t2 - t1) < 1e-6f || (t1 - t0) < 1e-6f || (t3 - t2) < 1e-6f) {
                // Degenerate → fall through to linear handling below.
            } else {
                // Estimate arc length with a 16-step Riemann sum.
                const int kEst = 16;
                float arcLen = 0.0f;
                QVector3D prev = A;
                for (int k = 1; k <= kEst; ++k) {
                    const float u = float(k) / float(kEst);
                    const float tc = t1 + u * (t2 - t1);
                    const QVector3D cur = catmullRomEval(P0, A, B, P3, t0, t1, t2, t3, tc);
                    arcLen += (cur - prev).length();
                    prev = cur;
                }
                if (arcLen < 1e-6f) {
                    raw.append({ A, seg, true });
                    continue;
                }
                const int nSteps = std::max(2, int(std::ceil(arcLen / ds)));
                for (int k = 0; k < nSteps; ++k) {
                    const float u  = float(k) / float(nSteps);
                    const float tc = t1 + u * (t2 - t1);
                    raw.append({ catmullRomEval(P0, A, B, P3, t0, t1, t2, t3, tc), seg, true });
                }
                continue;
            }
        }

        // -- Linear leg (also used when curve degenerates) --
        const float legLen = (B - A).length();
        if (legLen < 1e-6f) {
            raw.append({ A, seg, false });
            continue;
        }
        const int nSteps = std::max(1, int(std::ceil(legLen / ds)));
        for (int k = 0; k < nSteps; ++k) {
            const float u = float(k) / float(nSteps);
            raw.append({ A + u * (B - A), seg, false });
        }
    }
    // Final endpoint — last waypoint, exact.
    raw.append({ P[M - 1], M - 2, wps[M - 1].isCurve() && wps[M - 2].isCurve() });

    if (raw.size() < 2)
        return traj;

    const int N = raw.size();
    traj.m_samples.resize(N);

    // Time stamps
    for (int i = 0; i < N; ++i) {
        traj.m_samples[i].positionNed = raw[i].posNed;
        traj.m_samples[i].t = double(i) * dt;
    }

    // Velocities (forward difference; final = 0)
    for (int i = 0; i < N - 1; ++i) {
        traj.m_samples[i].velocityNed =
            (traj.m_samples[i + 1].positionNed - traj.m_samples[i].positionNed) * sampleHz;
    }
    traj.m_samples[N - 1].velocityNed = QVector3D(0.0f, 0.0f, 0.0f);

    // Yaw: curve samples are ALWAYS auto (atan2 of velocity); waypoint-leg samples
    // honor wps[srcLeg].yawAngle() if non-zero, else auto. Final sample copies previous.
    for (int i = 0; i < N - 1; ++i) {
        const Raw& r = raw[i];
        if (r.curveSample) {
            const QVector3D& v = traj.m_samples[i].velocityNed;
            traj.m_samples[i].yawRad = std::atan2(v.y(), v.x());
        } else {
            const float wpYawDeg = wps[r.srcLeg].yawAngle();
            if (std::abs(wpYawDeg) > 1e-6f) {
                traj.m_samples[i].yawRad = qDegreesToRadians(wpYawDeg);
            } else {
                const QVector3D& v = traj.m_samples[i].velocityNed;
                traj.m_samples[i].yawRad = std::atan2(v.y(), v.x());
            }
        }
    }
    traj.m_samples[N - 1].yawRad = (N >= 2) ? traj.m_samples[N - 2].yawRad : 0.0f;

    return traj;
}

// ---------------------------------------------------------------------------
// Preview helpers
// ---------------------------------------------------------------------------

QVector<QVector3D> Trajectory::previewSampledPositionsNed(
    const std::vector<Waypoint>& waypointsLogical,
    float cruiseSpeedMs,
    float sampleHz)
{
    Trajectory traj = fromWaypoints(waypointsLogical, cruiseSpeedMs, sampleHz);
    QVector<QVector3D> result;
    result.reserve(traj.m_samples.size());
    for (const TrajectorySample& s : traj.m_samples)
        result.append(s.positionNed);
    return result;
}

QVector<QVector3D> Trajectory::previewSampledPositionsLogical(
    const std::vector<Waypoint>& waypointsLogical,
    float cruiseSpeedMs,
    float sampleHz)
{
    // Build NED positions, then invert the NED flip: logical = (n, -e, -d)
    QVector<QVector3D> ned = previewSampledPositionsNed(waypointsLogical, cruiseSpeedMs, sampleHz);
    QVector<QVector3D> result;
    result.reserve(ned.size());
    for (const QVector3D& p : ned)
        result.append(QVector3D(p.x(), -p.y(), -p.z()));
    return result;
}

QStringList Trajectory::validate() const
{
    QStringList errors;

    if (m_samples.isEmpty()) {
        errors << QStringLiteral("Trajectory has no samples.");
        return errors;
    }

    // Check monotonically increasing t
    for (int i = 1; i < m_samples.size(); ++i) {
        if (m_samples[i].t <= m_samples[i - 1].t) {
            errors << QStringLiteral("Time is not monotonically increasing at sample %1.").arg(i);
        }
    }

    // Per-sample spatial and speed checks
    for (int i = 0; i < m_samples.size(); ++i) {
        const TrajectorySample& s = m_samples[i];
        float north = s.positionNed.x();
        float east  = s.positionNed.y();
        float down  = s.positionNed.z();

        if (std::abs(north) > 50.0f)
            errors << QStringLiteral("Sample %1: |north| = %2 m exceeds 50 m limit.").arg(i).arg(north, 0, 'f', 2);
        if (std::abs(east) > 50.0f)
            errors << QStringLiteral("Sample %1: |east| = %2 m exceeds 50 m limit.").arg(i).arg(east, 0, 'f', 2);
        // down in NED is negative altitude above ground; valid range: down in [-30, 0] means alt 0..30 m AGL
        if (down < -30.0f || down > 0.0f)
            errors << QStringLiteral("Sample %1: down = %2 m is outside [-30, 0] range.").arg(i).arg(down, 0, 'f', 2);

        float speed = s.velocityNed.length();
        if (speed > 5.0f)
            errors << QStringLiteral("Sample %1: speed = %2 m/s exceeds 5 m/s limit.").arg(i).arg(speed, 0, 'f', 2);
    }

    // Final velocity must be near zero
    const TrajectorySample& last = m_samples.last();
    if (last.velocityNed.length() >= 1e-3f) {
        errors << QStringLiteral("Final sample velocity |v| = %1 m/s is not zero.")
                      .arg(last.velocityNed.length(), 0, 'f', 4);
    }

    return errors;
}

QJsonObject Trajectory::toJson() const
{
    QJsonObject obj;
    obj["name"] = m_name;
    obj["hz"] = static_cast<double>(m_sampleHz);
    obj["cruise_ms"] = static_cast<double>(m_cruiseSpeedMs);

    QJsonArray samplesArray;
    for (const TrajectorySample& s : m_samples) {
        samplesArray.append(s.toJson());
    }
    obj["samples"] = samplesArray;

    return obj;
}

float Trajectory::duration() const
{
    if (m_samples.size() < 2)
        return 0.0f;
    return static_cast<float>(m_samples.last().t - m_samples.first().t);
}

float Trajectory::distance() const
{
    float total = 0.0f;
    for (int i = 1; i < m_samples.size(); ++i) {
        total += (m_samples[i].positionNed - m_samples[i - 1].positionNed).length();
    }
    return total;
}

float Trajectory::peakSpeedMs() const
{
    float peak = 0.0f;
    for (const TrajectorySample& s : m_samples) {
        float speed = s.velocityNed.length();
        if (speed > peak)
            peak = speed;
    }
    return peak;
}
