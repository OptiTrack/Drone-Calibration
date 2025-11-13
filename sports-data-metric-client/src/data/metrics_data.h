#pragma once

#include <QVector3D>
#include <QHash>

struct MetricsData {
    int id = -1;                    // Motive Frame ID
    QHash<QString, qreal> metrics;
};