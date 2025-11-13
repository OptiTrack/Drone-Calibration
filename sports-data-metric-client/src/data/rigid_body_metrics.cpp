#include "rigid_body_metrics.h"

#include <QJsonObject>

RigidBodyMetrics::RigidBodyMetrics(QObject* parent)
    : QObject(parent) {
}

MetricsData RigidBodyMetrics::computeMetricsForFrame(const FrameData& current, const FrameData& previous, const FrameData& secondPrevious) {
    MetricsData data;

    // Early exit if no asset selected
    if (selectedAsset == -1) {
        qDebug() << "No asset selected for metric computation.";
        return data;
    }

    for (size_t i = 0; i < current.rigidBodies.size(); ++i) 
    {
            const RigidBodyData& curr_rigid = current.rigidBodies[i];
            const RigidBodyData& prev_rigid = previous.rigidBodies[i];
            const RigidBodyData& sec_prev_rigid = secondPrevious.rigidBodies[i];

            if (curr_rigid.id != selectedAsset)
            {
                continue;
            }
        
            data.id = current.frameNumber;

            QVector3D orientation = curr_rigid.orientation.toEulerAngles();

        for (const QJsonValue& val : m_metricSettings) {
            QJsonObject metricObj = val.toObject();
            QString metricClass = metricObj["class"].toString();
            QJsonArray labels = metricObj["labels"].toArray();

            if (metricClass == "tilt") {
                qreal tilt = computeTilt(orientation);
                if (!labels.isEmpty())
                    data.metrics.insert(labels[0].toString(), tilt);

            } else if (metricClass == "velocity") {
                qreal velocity = computeVelocity(curr_rigid.position, prev_rigid.position,
                                                 current.timestamp - previous.timestamp);
                if (!labels.isEmpty())
                    data.metrics.insert(labels[0].toString(), velocity);

            } else if (metricClass == "acceleration") {
                qreal acceleration = computeAcceleration(curr_rigid.position, prev_rigid.position, sec_prev_rigid.position,
                                                         current.timestamp - previous.timestamp,
                                                         previous.timestamp - secondPrevious.timestamp);
                if (!labels.isEmpty())
                    data.metrics.insert(labels[0].toString(), acceleration);

            } else if (metricClass == "position") {
                QVector3D pos = curr_rigid.position;
                for (int j = 0; j < labels.size() && j < 3; ++j) {
                    data.metrics.insert(labels[j].toString(), pos[j]);
                }

            } else if (metricClass == "orientation") {
                for (int j = 0; j < labels.size() && j < 3; ++j) {
                    data.metrics.insert(labels[j].toString(), orientation[j]);
                }
            }

        }

            qDebug() << "RB metrics: " << data.metrics;

            return data;
    }
}

float RigidBodyMetrics::computeVelocity(const QVector3D& currentPosition, const QVector3D& previousPosition, double deltaTime) const
{
    // handle divide by 0 error
    if (deltaTime <= 0.0)
        return 0.0f;

    QVector3D distance = currentPosition - previousPosition;
    
    return distance.length() / deltaTime;
}

float RigidBodyMetrics::computeAcceleration(const QVector3D& currentPosition,
                                            const QVector3D& previousPosition,
                                            const QVector3D& secondPreviousPosition,
                                            double currDeltaTime,
                                            double prevDeltaTime) const
{
    // handle divide by 0 error
    if (currDeltaTime <= 0.0)
        return 0.0f;

    float curr_speed = computeVelocity(currentPosition, previousPosition, currDeltaTime);
    float prev_speed = computeVelocity(previousPosition, secondPreviousPosition, prevDeltaTime);
    float delta_speed = curr_speed - prev_speed;

    return delta_speed / currDeltaTime;
}

float RigidBodyMetrics::computeTilt(const QVector3D& eulerAngles) const
{
    float pitch = eulerAngles.x();
    float roll  = eulerAngles.z();

    // Compute total tilt angle from horizontal
    return std::sqrt(pitch * pitch + roll * roll);
}

void RigidBodyMetrics::setRigidBodyMap(const std::unordered_map<int, std::string> rigidBodies) {
    m_rigidBodies = rigidBodies;
}

void RigidBodyMetrics::createInverseMaps() {
    // Reverse m_rigidBodies: name -> ID
    for (const auto& [id, name] : m_rigidBodies) {
        m_rigidBodyNameToId.insert(QString::fromStdString(name), id);
    }
}

void RigidBodyMetrics::setAsset(QString rigidBodyAsset)
{
    if (m_rigidBodyNameToId.contains(rigidBodyAsset)) {
        selectedAsset = m_rigidBodyNameToId.value(rigidBodyAsset);
        qDebug() << "RigidBodyMetrics: Asset set to " << rigidBodyAsset << selectedAsset;
    } else {
        selectedAsset = NULL;
        qDebug() << "RigidBodyMetrics: Invalid asset.";
    }
}

const QMap<QString, int>& RigidBodyMetrics::getRigidBodyNameToId() const {
    return m_rigidBodyNameToId;
}

const std::unordered_map<int, std::string>& RigidBodyMetrics::getRigidBodyMap() const {
    return m_rigidBodies;
}

void RigidBodyMetrics::setMetricSettings(QJsonArray rigidMetricsSettings)
{
    m_metricSettings = rigidMetricsSettings;
}
