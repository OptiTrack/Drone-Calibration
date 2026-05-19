#include "pathfitter.h"

#include <QString>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

int trimLeadingStationary(const QVector<TimedSample> &raw, float eps, int run)
{
    if (raw.size() < 2)
        return 0;
    int consecutive = 0;
    int i = 0;
    for (; i + 1 < raw.size(); ++i) {
        const float step = (raw[i + 1].positionLogical - raw[i].positionLogical).length();
        if (step < eps) {
            if (++consecutive < run)
                continue;
        } else {
            // First non-stationary step found — keep the sample that begins moving.
            return i;
        }
    }
    return i;
}

int trimTrailingStationary(const QVector<TimedSample> &raw, int startIdx, float eps, int run)
{
    if (raw.size() - startIdx < 2)
        return raw.size() - 1;
    int consecutive = 0;
    int i = raw.size() - 1;
    for (; i > startIdx; --i) {
        const float step = (raw[i].positionLogical - raw[i - 1].positionLogical).length();
        if (step < eps) {
            if (++consecutive < run)
                continue;
        } else {
            return i;
        }
    }
    return startIdx;
}

QVector<QVector3D> smoothMovingAverage(const QVector<QVector3D> &pts, int radius)
{
    if (radius <= 0 || pts.size() < 3)
        return pts;
    QVector<QVector3D> out;
    out.resize(pts.size());
    const int nPts = static_cast<int>(pts.size());
    for (int i = 0; i < nPts; ++i) {
        const int lo = std::max(0, i - radius);
        const int hi = std::min(nPts - 1, i + radius);
        // Accumulate in double to preserve precision for sub-meter / sub-cm traces.
        double ax = 0.0, ay = 0.0, az = 0.0;
        int n = 0;
        for (int k = lo; k <= hi; ++k) {
            ax += pts[k].x();
            ay += pts[k].y();
            az += pts[k].z();
            ++n;
        }
        if (n > 0) {
            const double inv = 1.0 / double(n);
            out[i] = QVector3D(float(ax * inv), float(ay * inv), float(az * inv));
        } else {
            out[i] = pts[i];
        }
    }
    return out;
}

// Perpendicular distance from point p to the infinite line through a-b.
float perpendicularDistance3D(const QVector3D &p, const QVector3D &a, const QVector3D &b)
{
    const QVector3D ab = b - a;
    const float abLen = ab.length();
    if (abLen < 1e-6f)
        return (p - a).length();
    const QVector3D cross = QVector3D::crossProduct(p - a, ab);
    return cross.length() / abLen;
}

// Iterative Ramer-Douglas-Peucker — returns sorted indices of surviving points.
std::vector<int> rdpIndices(const QVector<QVector3D> &pts, float toleranceM)
{
    std::vector<int> keep;
    if (pts.size() < 2)
        return keep;
    std::vector<bool> survives(pts.size(), false);
    survives.front() = true;
    survives.back() = true;

    // Stack of [lo, hi] index ranges to examine.
    std::vector<std::pair<int, int>> stack;
    stack.emplace_back(0, pts.size() - 1);
    while (!stack.empty()) {
        const auto [lo, hi] = stack.back();
        stack.pop_back();
        if (hi - lo < 2)
            continue;
        float maxDist = -1.0f;
        int maxIdx = -1;
        for (int i = lo + 1; i < hi; ++i) {
            const float d = perpendicularDistance3D(pts[i], pts[lo], pts[hi]);
            if (d > maxDist) {
                maxDist = d;
                maxIdx = i;
            }
        }
        if (maxIdx >= 0 && maxDist > toleranceM) {
            survives[maxIdx] = true;
            stack.emplace_back(lo, maxIdx);
            stack.emplace_back(maxIdx, hi);
        }
    }

    keep.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i) {
        if (survives[i])
            keep.push_back(i);
    }
    return keep;
}

} // namespace

PathFitResult fitPathFromSamples(const QVector<TimedSample> &raw, const PathFitOptions &opts)
{
    PathFitResult result;
    result.rawSampleCount = raw.size();

    if (raw.size() < 2)
        return result;

    // 1. Trim stationary head and tail.
    const int startIdx = trimLeadingStationary(raw, opts.stationaryEpsM, opts.stationaryRun);
    const int endIdx = trimTrailingStationary(raw, startIdx, opts.stationaryEpsM, opts.stationaryRun);
    if (endIdx - startIdx < 1)
        return result; // Entirely stationary.

    const int n = endIdx - startIdx + 1;
    result.trimmedSampleCount = n;

    // 2. Backbone = cleaned (post-trim, pre-smooth) raw positions.
    QVector<QVector3D> cleanedRaw;
    cleanedRaw.reserve(n);
    for (int i = startIdx; i <= endIdx; ++i)
        cleanedRaw.append(raw[i].positionLogical);
    result.backboneLogical = cleanedRaw;

    // 2b. Auto-scale the RDP tolerance and corner-radius bounds to the trace extent so the
    //     fitter behaves correctly on both 3 m loops and 30 cm hover-arounds. Doubles
    //     across the reduction so sub-mm coordinates don't lose precision in the diagonal.
    double minX = cleanedRaw.first().x(), maxX = minX;
    double minY = cleanedRaw.first().y(), maxY = minY;
    double minZ = cleanedRaw.first().z(), maxZ = minZ;
    for (const QVector3D &p : cleanedRaw) {
        minX = std::min(minX, double(p.x())); maxX = std::max(maxX, double(p.x()));
        minY = std::min(minY, double(p.y())); maxY = std::max(maxY, double(p.y()));
        minZ = std::min(minZ, double(p.z())); maxZ = std::max(maxZ, double(p.z()));
    }
    const double dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
    const float D = float(std::sqrt(dx * dx + dy * dy + dz * dz));

    // Tolerance: smaller of the user ceiling and 2% of D; floored at 3 mm.
    const float effTolerance  = std::max(0.003f, std::min(opts.toleranceM, 0.02f * D));
    // Corner radius range, expressed as a fraction of D, but never larger than the user ceilings.
    const float effMinCornerR = std::min(opts.minCornerRadM, std::max(0.005f, 0.03f * D));
    const float effMaxCornerR = std::min(opts.maxCornerRadM, std::max(0.05f,  0.40f * D));

    // 3. Estimate sample rate and smooth.
    float hz = 2.0f;
    const double tSpan = raw[endIdx].t - raw[startIdx].t;
    if (tSpan > 1e-3 && n > 1)
        hz = static_cast<float>((n - 1) / tSpan);
    const int smoothRadius = std::max(1, static_cast<int>(std::ceil(0.5f * opts.smoothWindowSec * hz)));
    const QVector<QVector3D> smoothed = smoothMovingAverage(cleanedRaw, smoothRadius);

    // 4. RDP simplify in 3D with the scale-adaptive tolerance.
    const std::vector<int> survivors = rdpIndices(smoothed, effTolerance);
    if (survivors.size() < 2) {
        // Degenerate: emit a single NAV waypoint at the cleaned midpoint.
        Waypoint w(smoothed[smoothed.size() / 2]);
        w.setWaypointType(QStringLiteral("NAV_WAYPOINT"));
        w.setAcceptanceRadius(opts.defaultAcceptanceRadiusM);
        w.setHoldTime(0.0f);
        w.setPassThrough(false);
        w.setSequence(0);
        w.setName(QStringLiteral("rec_0"));
        result.waypoints.push_back(std::move(w));
        return result;
    }

    // 5. Build waypoints. Endpoints = NAV, interior = CURVE with derived corner radius.
    result.waypoints.reserve(survivors.size());
    for (size_t k = 0; k < survivors.size(); ++k) {
        const int idx = survivors[k];
        const int origIdx = startIdx + idx;
        const bool isEndpoint = (k == 0) || (k + 1 == survivors.size());

        Waypoint w(smoothed[idx]);
        w.setSequence(static_cast<int>(k));
        w.setName(QStringLiteral("rec_%1").arg(k));
        w.setAcceptanceRadius(opts.defaultAcceptanceRadiusM);
        w.setHoldTime(0.0f);
        w.setPassThrough(false);
        w.setYawAngle(raw[origIdx].yawDeg); // No-op on CURVE per waypoint.cpp; stored on NAV endpoints.

        if (isEndpoint) {
            w.setWaypointType(QStringLiteral("NAV_WAYPOINT"));
        } else {
            // Variable corner radius from the local turn geometry — gentle bends get small arcs
            // that hug the original recording, sharp bends get the largest arc that still fits
            // between V's neighbors. This is the "curve of best fit through all the points"
            // behavior, with adaptive bounds derived from the trace extent.
            const int prevIdx = survivors[k - 1];
            const int nextIdx = survivors[k + 1];
            const QVector3D V = smoothed[idx];
            const QVector3D P = smoothed[prevIdx];
            const QVector3D N = smoothed[nextIdx];
            const QVector3D vp = (P - V);
            const QVector3D vn = (N - V);
            const float lenVP = vp.length();
            const float lenVN = vn.length();

            float r = effMinCornerR;
            if (lenVP > 1e-6f && lenVN > 1e-6f) {
                const QVector3D vpUnit = vp / lenVP;
                const QVector3D vnUnit = vn / lenVN;
                const float cosTheta = std::clamp(QVector3D::dotProduct(vpUnit, vnUnit), -1.0f, 1.0f);
                const float theta = std::acos(cosTheta);              // interior angle at V
                // Half of the *turn* angle = (pi - theta) / 2.
                const float halfTurn = 0.5f * (float(M_PI) - theta);
                // Avoid blow-up for near-collinear (theta -> pi, halfTurn -> 0) and near-U-turn cases.
                const float halfTan = std::tan(std::clamp(halfTurn, 1e-3f, float(M_PI) * 0.5f - 1e-3f));
                const float legCap = 0.5f * std::min(lenVP, lenVN);
                const float rGeom = legCap * halfTan;
                r = std::clamp(rGeom, effMinCornerR, effMaxCornerR);
            }
            w.setCurve(true);
            w.setCornerRadius(r);
        }
        result.waypoints.push_back(std::move(w));
    }

    return result;
}
