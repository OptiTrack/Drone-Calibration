#include "metricsmanager.h"

MetricsManager::MetricsManager(QWidget* parent, QString managerType)
    : m_parent(parent)
{
    if (managerType == "rigidMetricsManager" || managerType == "bodyMetricsManager") {
        m_managerType = managerType;
    } else {
        qWarning() << "MetricsManager: Invalid manager type" << managerType;
        throw std::invalid_argument("Invalid manager type: " + managerType.toStdString());
    }
}

void MetricsManager::addMetricController(const QString name, const QString units, QVector<QString> labels, QVector<QString> descriptions, QVector<bool> graphs)
{
    MetricWidgets *metricWidgets = uiFactory.createMetricWidgets(name, units, labels, descriptions, graphs);
    addGroupBoxToUI(m_parent, metricWidgets->groupBox);

    MetricController* metricController = new MetricController(metricWidgets);
    metricControllers.insert(metricWidgets->name, metricController);
}

void MetricsManager::deleteMetricControllers()
{
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(m_parent->layout());

    for (MetricController *metricController : metricControllers) {
        MetricWidgets *metricWidgets = metricController->getMetricWidgets();

        if (metricWidgets && metricWidgets->groupBox) {
            if (layout) {
                layout->removeWidget(metricWidgets->groupBox);
            }
            metricWidgets->groupBox->setParent(nullptr);
            metricWidgets->groupBox->deleteLater();
        }

        delete metricController;
    }
    metricControllers.clear();
}

void MetricsManager::setMetricSettings(QJsonArray newMetricSettings)
{
    if (metricControllers.size() > 0) {
        deleteMetricControllers();
    }

    metricSettings = newMetricSettings;

    for (int i = 0; i < metricSettings.count(); ++i) {
        QJsonObject currentMetric = metricSettings[i].toObject();

        QString name = currentMetric["name"].toString();
        QString units = currentMetric["units"].toString();

        QVector<QString> labels, descriptions;
        QVector<bool> graphs;

        for (const QJsonValue &value : currentMetric["labels"].toArray()) {
            labels.append(value.toString());
        }

        for (const QJsonValue &value : currentMetric["descriptions"].toArray()) {
            descriptions.append(value.toString());
        }

        if (currentMetric.contains("configuration")) {
            QJsonObject configuration = currentMetric["configuration"].toObject();

            for (const QJsonValue &value : configuration["isGraph"].toArray()) {
                graphs.append(value.toBool());
            }
        }

        addMetricController(name, units, labels, descriptions, graphs);
    }
}

void MetricsManager::onMetricsComputed(MetricsData rigidBodyMetrics, MetricsData skeletonMetrics)
{
    if (m_managerType == "rigidMetricsManager") {
        updateMetricControllers(rigidBodyMetrics.id, rigidBodyMetrics.metrics);
    } else {
        updateMetricControllers(skeletonMetrics.id, skeletonMetrics.metrics);
    }
}

void MetricsManager::onUpdatedMetricSettings(QJsonArray rigidMetricSettings, QJsonArray bodyMetricSettings)
{
    if (m_managerType == "rigidMetricsManager") {
        setMetricSettings(rigidMetricSettings);
    } else {
        setMetricSettings(bodyMetricSettings);
    }
}

void MetricsManager::updateMetricControllers(qreal id, QHash<QString, qreal> metrics)
{
    for (auto it = metricControllers.begin(); it != metricControllers.end(); ++it) {
        it.value()->addData(id, metrics);
    }
}
