#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QObject>
#include <QVector>
#include <QJsonArray>
#include "frame_data.h"
#include "metrics_data.h"

/**
 * @brief Computes skeletal joint-based metrics from motion capture frame data.
 * 
 * SkeletonMetrics processes motion capture skeleton data to compute high-level
 * kinematic measurements such as joint angles and forward tilt. It supports
 * customizable joint mappings defined in a JSON config file and provides
 * reverse lookup maps from bone/skeleton names to IDs.
 */
class SkeletonMetrics : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs a SkeletonMetrics object.
     * 
     * Initializes the lookup maps and joint configuration used for computing metrics.
     * 
     * @param parent Optional QObject parent.
     */
    SkeletonMetrics(QObject* parent = nullptr);

    /**
     * @brief Computes skeleton-based joint metrics for a single frame.
     * 
     * Uses the configured joint mappings to extract joint angles (e.g., knee, elbow)
     * and other skeletal features from the provided FrameData.
     * 
     * @param current The current FrameData containing skeleton information.
     * @return A QVector of SkeletonMetricsData, one for each detected skeleton.
     */
    MetricsData computeMetricsForFrame(const FrameData& current);

    /**
     * @brief Retrieves the name-to-ID map for skeletons.
     * 
     * @return A reference to the internal QMap from skeleton names to their IDs.
     */
    const QMap<QString, int>& getSkeletonNameToId() const;

    const std::unordered_map<int, std::string>& getSkeletonNameMap() const;
    const std::unordered_map<int, std::unordered_map<int, std::string>>& getBoneNameMap() const;

    /**
     * @brief Creates reverse lookup maps (name → ID) for rigid bodies, skeletons, and bones.
     */
    void createInverseMaps();

    /**
     * @brief Sets the skeleton ID-to-name map.
     * 
     * @param skeletons Map from skeleton IDs to their names.
     */
    void setSkeletonMap(const std::unordered_map<int, std::string> skeletons);

    /**
     * @brief Sets the bone ID-to-name map for each skeleton.
     * 
     * @param bones Map from skeleton ID to a map of bone ID to bone name.
     */
    void setBoneMap(const std::unordered_map<int, std::unordered_map<int, std::string>> bones);

    /**
     * @brief Sets the skeleton asset name or path.
     *
     * @param skeletonAsset The asset identifier or path as a QString.
     */
    void setAsset(QString skeletonAsset);

    /**
     * @brief Sets the configuration for skeleton metric calculations.
     *
     * @param skeletonMetricsSettings A QJsonArray containing metric definitions and settings.
     */
    void setMetricSettings(QJsonArray skeletonMetricsSettings);

    /**
     * @brief Sets the naming convention to use for bones, skeletons, etc.
     * 
     * This can be used to support custom naming styles.
     * @param convention A QString identifier for the naming style.
     */
    void setNamingConvention(const QString& convention);

private:
    QMap<QString, QStringList> jointMappings;  // Maps joint names to alist of bone names used for that joint
    QString configFilePath;                    // Path to the joint configuration JSON file

    QString m_namingConvention = "";

    int selectedAsset = 0;  // ID of selected skeleton asset
    QJsonArray m_metricSettings; // Metric settings for current sport

    std::unordered_map<int, std::string> m_skeletons;   // Skeleton ID-to-name map 
    std::unordered_map<int, std::unordered_map<int, std::string>> m_bones; // Bone ID-to-name maps

    QMap<QString, int> m_skeletonNameToId;     // Map of skeleton name → ID (reverse of m_skeletons)
    QMap<int, QMap<QString, int>> m_boneNameToId; // Map of skeleton ID → (bone name → ID), reversed from m_bones

    /**
     * @brief Loads the joint configuration from a JSON file.
     * 
     * Parses joint-to-bone mappings from the given config file and populates jointMappings.
     * 
     * @param filePath The path to the JSON configuration file.
     * @return True if the config was successfully loaded and parsed; false otherwise.
     */
    bool loadConfiguration(const QString& filePath);

    /**
     * @brief Computes the angle between two bones in a skeleton.
     *
     * Calculates the joint angle using the orientations of two specified bones.
     *
     * @param bone1Id ID of the first bone.
     * @param bone2Id ID of the second bone.
     * @param skeleton The skeleton data containing bone orientations.
     * @return The computed angle between the two bones, in degrees.
     */
    float getJointAngle(int bone1Id, int bone2Id, const SkeletonData& skeleton);

    /**
     * @brief Computes the angle between two bone orientations using quaternions.
     * 
     * This method calculates the relative rotation between two quaternions and converts
     * it to a single scalar angle.
     * 
     * @param bone1Orientation Orientation of the first bone (QQuaternion).
     * @param bone2Orientation Orientation of the second bone (QQuaternion).
     * @return The angle between the two bones in degrees.
     */
    float computeJointAngle(const QQuaternion& bone1Orientation, const QQuaternion& bone2Orientation);
 
    /**
     * @brief Computes the forward tilt (horizontal distance) between two bones in a skeleton.
     *
     * Measures the horizontal distance between two bone positions.
     *
     * @param bone1Id ID of the first bone.
     * @param bone2Id ID of the second bone.
     * @param skeleton The skeleton data containing bone positions.
     * @return The computed forward tilt distance in centimeters.
     */
    float computeForwardTilt(int bone1Id, int bone2Id, const SkeletonData& skeleton);

signals:
    /**
     * @brief Emitted when new skeleton metric data has been computed.
     * 
     * Used to send calculated skeleton metrics (e.g., joint angles) to the UI
     * or downstream processing systems.
     * 
     * @param metrics A QVector of SkeletonData containing computed values.
     */
    void skeletonMetricsComputed(const QVector<SkeletonData>& metrics);
};