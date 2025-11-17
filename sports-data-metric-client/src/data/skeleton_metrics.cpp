#include "skeleton_metrics.h"
#include "metrics_data.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>

SkeletonMetrics::SkeletonMetrics(QObject* parent)
    : QObject(parent)
{
}

MetricsData SkeletonMetrics::computeMetricsForFrame(const FrameData& current) {
    MetricsData data;

    // Early exit if no asset selected
    if (selectedAsset == -1) {
        qDebug() << "No asset selected for metric computation.";
        return data;
    }
    
    for (size_t i = 0; i < current.skeletons.size(); ++i) {
        data.id = current.frameNumber;

        for (const QJsonValue& val : m_metricSettings) {
            QJsonObject metricObj = val.toObject();
            QString metricClass = metricObj["class"].toString();
            QJsonArray ids = metricObj["ids"].toArray();
            QJsonArray labels = metricObj["labels"].toArray();

            if (metricClass == "angle" && ids.size() >= 2 && !labels.isEmpty()) {
                int id1 = ids[0].toInt();
                int id2 = ids[1].toInt();
                qreal angle = getJointAngle(id1, id2, current.skeletons[i]);
                data.metrics.insert(labels[0].toString(), angle);
            }
            else if (metricClass == "distance" && ids.size() >= 2 && !labels.isEmpty()) {
                int id1 = ids[0].toInt();
                int id2 = ids[1].toInt();
                qreal distance = computeForwardTilt(id1, id2, current.skeletons[i]);
                data.metrics.insert(labels[0].toString(), distance);
            }
        }

        qDebug() << "Skeleton metrics: " << data.metrics;
        return data; 
    }
}

float SkeletonMetrics::getJointAngle(int bone1Id, int bone2Id, const SkeletonData& skeleton)
{
    return computeJointAngle(
        skeleton.bones[bone1Id].orientation,
        skeleton.bones[bone2Id].orientation
    );
}

float SkeletonMetrics::computeJointAngle(const QQuaternion& bone1Orientation, const QQuaternion& bone2Orientation) 
{
    // Compute the relative rotation from bone1 to bone2
    QQuaternion relativeRotation = bone1Orientation.conjugated() * bone2Orientation;

    // Ensure the quaternion is normalized
    relativeRotation.normalize();

    // Extract the angle (in radians) from the relative rotation quaternion
    float angleRadians = 2.0f * qAcos(relativeRotation.scalar());

    // Convert the angle to degrees
    float angleDegrees = qRadiansToDegrees(angleRadians);

    return angleDegrees;
}

float SkeletonMetrics::computeForwardTilt(int bone1Id, int bone2Id, const SkeletonData& skeleton) 
{
    const QVector3D& pos1 = skeleton.bones[bone1Id].position;
    const QVector3D& pos2 = skeleton.bones[bone2Id].position;

    float dx = pos2.x() - pos1.x();
    float dz = pos2.z() - pos1.z();
    return std::sqrt(dx * dx + dz * dz) * 100.0f;  // horizontal distance in cm
}

const QMap<QString, int>& SkeletonMetrics::getSkeletonNameToId() const {
    return m_skeletonNameToId;
}

const std::unordered_map<int, std::string>& SkeletonMetrics::getSkeletonNameMap() const 
{
    return m_skeletons;
}

const std::unordered_map<int, std::unordered_map<int, std::string>>& SkeletonMetrics::getBoneNameMap() const 
{
    return m_bones;
}

void SkeletonMetrics::createInverseMaps() {
    // Reverse m_skeletons: name -> ID
    for (const auto& [id, name] : m_skeletons) {
        m_skeletonNameToId.insert(QString::fromStdString(name), id);
    }

    // Reverse m_bones: for each skeleton ID, reverse its bone map
    for (const auto& [skeletonId, boneMap] : m_bones) {
        QMap<QString, int> reversedBoneMap;
        for (const auto& [boneId, boneName] : boneMap) {
            reversedBoneMap.insert(QString::fromStdString(boneName), boneId);
        }
        m_boneNameToId.insert(skeletonId, reversedBoneMap);
    }
}

void SkeletonMetrics::setSkeletonMap(const std::unordered_map<int, std::string> skeletons) {
    m_skeletons = skeletons;
}

void SkeletonMetrics::setBoneMap(const std::unordered_map<int, std::unordered_map<int, std::string>> bones) {
    m_bones = bones;
}

void SkeletonMetrics::setAsset(QString skeletonAsset)
{
    if (m_skeletonNameToId.contains(skeletonAsset)){
        selectedAsset = m_skeletonNameToId.value(skeletonAsset);
        qDebug() << "SkeletonMetrics: Asset set to " << skeletonAsset << selectedAsset;
    } else {
        qDebug() << "SkeletonMetrics: Invalid asset.";
        selectedAsset = -1;
    }
}

void SkeletonMetrics::setNamingConvention(const QString& convention) 
{
    qDebug() << "Setting naming convention to:" << convention;
    m_namingConvention = convention;
    configFilePath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../src/data/skeleton_config.json");
    loadConfiguration(configFilePath);
}

bool SkeletonMetrics::loadConfiguration(const QString& filePath) {
    qDebug() << "Loading config from:" << filePath;

    // Open the configuration file for reading
    QFile file(configFilePath);
    file.open(QIODevice::ReadOnly);

    // Read the entire contents of the file into a byte array
    QByteArray jsonData = file.readAll();

    // Attempt to parse the JSON data and check for errors
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << parseError.errorString();
        return false;
    }

    // Delete the old join mapping
    jointMappings.clear();

    // Extract the root object
    QJsonObject rootObj = doc.object();

    // Extract the joints 
    QJsonObject conventionObj = rootObj.value(m_namingConvention).toObject();
    QJsonObject jointsObj = conventionObj.value("joints").toObject();

    // Iterate through each joint
    for (const QString& key : jointsObj.keys()) {
        QJsonArray boneArray = jointsObj.value(key).toArray(); // Get array of bones
        QStringList boneNames;

        // Convert JSON array elements to strings
        for (const QJsonValue& val : boneArray) {
            boneNames.append(val.toString());
        }

        // Insert the list of bone names into the jointMappings map under the metric
        jointMappings.insert(key, boneNames);
    }

    return true;
}

void SkeletonMetrics::setMetricSettings(QJsonArray skeletonMetricsSettings)
{
    m_metricSettings = skeletonMetricsSettings;
}