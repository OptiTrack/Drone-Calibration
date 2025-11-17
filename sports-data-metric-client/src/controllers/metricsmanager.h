#ifndef METRICSMANAGER_H
#define METRICSMANAGER_H

#include <QObject>
#include <QThread>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>

#include "rigid_body_metrics.h"
#include "skeleton_metrics.h"
#include "metricscontroller.h"
#include "uifactory.h"
#include "toggles.h"
#include "./src/utils/uiutils.h"

class MetricsManager : public QObject {
    Q_OBJECT

public:
    MetricsManager(QWidget* parent, QString type);
    void addMetricController(const QString name, const QString units, QVector<QString> labels, QVector<QString> descriptions, QVector<bool> graphs);
    void deleteMetricControllers();
    void setMetricSettings(QJsonArray newMetricSettings);

public slots:
    void onMetricsComputed(MetricsData rigidBodyMetrics, MetricsData skeletonMetrics);
    void onUpdatedMetricSettings(QJsonArray rigidMetricSettings, QJsonArray bodyMetricSettings);

private:
    void updateMetricControllers(qreal id, QHash<QString, qreal> metrics);

    QWidget* m_parent;
    QString m_managerType;

    UiFactory uiFactory;
    QMap<QString, MetricController*> metricControllers;
    QJsonArray metricSettings;
};

#endif // METRICSMANAGER_H
