#ifndef PATHFITTER_H
#define PATHFITTER_H

#include <QVector>
#include <QVector3D>
#include <vector>

#include "../models/waypoint.h"

struct TimedSample {
    double t;                  // seconds from start of recording
    QVector3D positionLogical; // X-forward, Y-left, Z-up
    float yawDeg;
};

struct PathFitOptions {
    float toleranceM      = 0.15f;  // RDP perpendicular tolerance (m); matches default acceptance_radius
    float smoothWindowSec = 0.5f;   // centered moving-average window
    float stationaryEpsM  = 0.05f;  // |Δp| threshold for trim
    int   stationaryRun   = 4;      // consecutive sub-eps steps that count as "stationary"
    float minCornerRadM   = 0.10f;
    float maxCornerRadM   = 0.75f;
    float defaultAcceptanceRadiusM = 0.15f;
};

struct PathFitResult {
    std::vector<Waypoint> waypoints;            // first + last = NAV_WAYPOINT; interior = CURVE
    QVector<QVector3D>    backboneLogical;      // post-trim raw trace for the overlay
    int                   rawSampleCount = 0;
    int                   trimmedSampleCount = 0;
};

/// Reverse-engineer a curve-point planner path from a recorded manual flight.
PathFitResult fitPathFromSamples(const QVector<TimedSample> &raw,
                                 const PathFitOptions &opts = PathFitOptions());

#endif // PATHFITTER_H
