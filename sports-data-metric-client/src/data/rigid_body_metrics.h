#pragma once

#include <QObject>
#include <QVector>
#include <QJsonArray>
#include "frame_data.h"
#include "metrics_data.h"

/**
 * @brief Computes per-rigid-body motion metrics for each frame.
 *
 * This class calculates metrics such as velocity, acceleration, and tilt
 * for each rigid body in the current frame. It compares the current frame
 * with previous frames to extract motion data and emits the computed results.
 */
class RigidBodyMetrics : public QObject {
    Q_OBJECT

public:
    explicit RigidBodyMetrics(QObject* parent = nullptr);

    /**
     * @brief Constructs a RigidBodyMetrics processor.
     * @param parent Optional Qt parent object.
     */
    MetricsData computeMetricsForFrame(const FrameData& current,
                                                        const FrameData& previous,
                                                        const FrameData& secondPrevious);

    /**
     * @brief Creates reverse lookup maps (name → ID) for rigid bodies, skeletons, and bones.
     */
    void createInverseMaps();

    /**
     * @brief Sets the rigid body ID-to-name map.
     *
     * @param rigidBodies A map from rigid body ID to name.
     */                                                        
    void setRigidBodyMap(const std::unordered_map<int, std::string> rigidBodies);

    /**
     * @brief Returns the map from rigid body name to ID.
     *
     * @return A constant reference to the name-to-ID map.
     */
    const QMap<QString, int>& getRigidBodyNameToId() const;

    const std::unordered_map<int, std::string>& getRigidBodyMap() const;

    /* @brief Sets the rigid body asset name or path.
    *
    * @param rigidBodyAsset The asset identifier or path as a QString.
    */
    void setAsset(QString rigidBodyAsset);

    /**
     * @brief Sets the configuration for rigid body metric calculations.
     *
     * @param rigidMetricsSettings A QJsonArray containing metric definitions and settings.
     */
    void setMetricSettings(QJsonArray rigidMetricsSettings);

private:
    /**
     * @brief Computes velocity between two positions over a time delta.
     *
     * @param currentPosition Position in the current frame.
     * @param previousPosition Position in the previous frame.
     * @param deltaTime Time elapsed between the two frames.
     * @return Velocity in units per second.
     */
    float computeVelocity(const QVector3D& currentPosition,
                          const QVector3D& previousPosition,
                          double deltaTime) const;

    /**
     * @brief Computes acceleration from three positions and time deltas.
     *
     * Uses finite difference between two computed velocities to estimate acceleration.
     *
     * @param currentPosition Position in the current frame.
     * @param previousPosition Position in the previous frame.
     * @param secondPreviousPosition Position two frames ago.
     * @param currDeltaTime Time between current and previous.
     * @param prevDeltaTime Time between previous and second previous.
     * @return Acceleration in units per second squared.
     */
    float computeAcceleration(const QVector3D& currentPosition,
                              const QVector3D& previousPosition,
                              const QVector3D& secondPreviousPosition,
                              double currDeltaTime,
                              double prevDeltaTime) const;

    /**
     * @brief Computes the forward tilt angle from Euler rotation.
     *
     * Typically uses the X or Z rotation component depending on the coordinate system.
     *
     * @param eulerAngles The rotation angles (pitch, yaw, roll).
     * @return Tilt angle in degrees or radians (depending on usage).
     */
    float computeTilt(const QVector3D& eulerAngles) const;

    int selectedAsset = 0;  // ID of selected rigid body asset
    QJsonArray m_metricSettings; // Metric settings for current sport
    std::unordered_map<int, std::string> m_rigidBodies; // Rigid body ID-to-name map 
    QMap<QString, int> m_rigidBodyNameToId;    // Map of rigid body name → ID (reverse of m_rigidBodies)


signals:
    /**
     * @brief Signal emitted when metrics have been computed for a frame.
     *
     * @param metrics A vector of computed metric data for each rigid body.
     */
    void metricsComputed(const QVector<MetricsData>& metrics);
};