#pragma once

#include <QObject>
#include <unordered_map>
#include <vector>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>

#include "skeleton_metrics.h"
#include "rigid_body_metrics.h"
#include "frame_data.h"
#include "../controllers/streamingcontroller.h"
#include "../controllers/configurecontroller.h"

/**
 * @brief Orchestrates the processing of motion capture frames.
 *
 * The DataProcessor coordinates rigid body and skeleton metric calculations.
 * It holds references to the current frame data and entity lookup tables,
 * and triggers metric computation when frames are updated.
 */
class DataProcessor : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs the data processor with reference data and frame history.
     * 
     * @param frames Reference to the frame history buffer.
     * @param parent Optional parent QObject.
     */
    explicit DataProcessor(
        const std::vector<FrameData>& frames,
        QObject* parent = nullptr);  


    /**
     * @brief Gets the map of rigid body IDs to names.
     *   
     * @return A map from rigid body ID to name.
     */
    std::unordered_map<int, std::string> getRigidBodyMap();

    /**
     * @brief Gets the skeleton name map.
     * 
     * @return A reference to the map from skeleton ID to skeleton name.
     */
    const std::unordered_map<int, std::string>& getSkeletonNameMap() const;

    /**
     * @brief Gets the nested bone name map.
     * 
     * @return A reference to the map from skeleton ID to a map of bone IDs to bone names.
     */
    const std::unordered_map<int, std::unordered_map<int, std::string>>& getBoneNameMap() const;

    /**
     * @brief Retrieves the list of all processed frames.
     * 
     * @return A reference to the frame history buffer.
     */
    const std::vector<FrameData>& getFrames();


public slots:
    /**
     * @brief Slot called when new frame data is available.
     * 
     * Triggers computation of rigid body and skeleton metrics
     * for the latest frames.
     */
    void onFramesUpdated(const FrameData& latestFrame);

    /**
     * @brief Slot to receive updated asset ID-to-name maps.
     * 
     * Updates internal maps for rigid bodies, skeletons, and bones.
     */
    void receiveMaps(const std::unordered_map<int, std::string>& rigidBodies,
                     const std::unordered_map<int, std::string>& skeletons,
                     const std::unordered_map<int, std::unordered_map<int, std::string>>& bones);

    /**
     * @brief Slot to receive metric configuration from the UI.
     * 
     * Stores rigid and body metric settings for use in later computations.
     */
    void receiveMetricSettings(QJsonArray rigidMetricsSettings, QJsonArray bodyMetricsSettings);
    
    /**
     * @brief Slot to receive the selected asset settings from the UI.
     * 
     * Stores the current user-selected rigid body or skeleton asset
     * used for computing metrics.
    */
    void receiveAssets(AssetSettings assetSettings);

    /**
     * @brief Slot to receive the selected naming convention settings from the UI.
     * 
     * Sets the bone naming convention  of the corresponding configuration.
     */
    void receiveNamingConvention(ConnectionSettings connectionSettings);

    // void receiveTakeData(QVector<QVector<QPair<int, int>>> skeletonBones, int maxBones, int maxJoints);


signals:
    /**
     * @brief Signal emitted when metrics have been computed for a frame.
     *
     * @param metrics A vector of computed metric data for each rigid body.
     */
    void metricsComputed(MetricsData rigidBodyMetrics, MetricsData skeletonMetrics);

    /**
     * @brief Signal emitted when rigid body and skeleton name-ID maps are ready.
     *
     * @param skeletons Map of skeleton names to IDs.
     * @param rigidBodies Map of rigid body names to IDs.
     */
    void sendAssets(const QMap<QString, int>& skeletons,
                     const QMap<QString, int>& rigidBodies);

private:
    std::unique_ptr<SkeletonMetrics> skeletonMetrics;         // Skeleton metric processor
    RigidBodyMetrics rigidBodyMetrics;                        // Rigid body metric processor

    const std::vector<FrameData>& m_frames;     // Reference to frame history buffer

    std::optional<FrameData> m_previousFrame;       // previous processed frame
    std::optional<FrameData> m_secondPreviousFrame; // previous processed frame
};

