// QObject wrapper class for NatNetConnection.
// Manages connection control from a separate thread and provides access to motion capture data.

#pragma once

#include <QObject>
#include "natnet_connection.h"
#include "../controllers/streamingcontroller.h"

class ConnectionController : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs a ConnectionController object.
     * @param parent Optional parent QObject.
     */
    explicit ConnectionController(QObject* parent = nullptr);

    /**
     * @brief Gets the list of captured motion frames.
     * @return A constant reference to the vector of FrameData.
     */
    const std::vector<FrameData>& getFrames() const;

    /**
     * @brief Gets the mapping from rigid body IDs to their corresponding names.
     * @return A constant reference to the rigid body ID-to-name map.
     */
    const std::unordered_map<int, std::string>& getRigidBodyIdToName() const;

    /**
     * @brief Gets the mapping from skeleton IDs to their corresponding names.
     * @return A constant reference to the skeleton ID-to-name map.
     */
    const std::unordered_map<int, std::string>& getSkeletonIdToName() const;

    /**
     * @brief Gets the mapping from skeleton IDs and bone IDs to their corresponding names.
     * @return A constant reference to the nested skeleton-to-bone ID-to-name map.
     */
    const std::unordered_map<int, std::unordered_map<int, std::string>>& getBoneIdToName() const;

    /**
     * @brief Gets the data descriptions of the rigid bodies in the scene.
     * @return The data descriptions object.
     */
    sDataDescriptions* getDataDescriptions();

public slots:
    /**
     * @brief Starts the NatNet server connection.
     */
    void startConnection(ConnectionSettings connectionSettings);

    /**
     * @brief Stops the NatNet server connection.
     */
    void stopConnection();

    void replayFrame(FrameData frame);

private:
    NatNetConnection connection;  // Underlying NatNet connection object

signals:
    /**
     * @brief Emitted when a new frame of motion capture data is received.
     *
     * This signal delivers the latest processed FrameData to any connected components,
     * such as data processors or rendering systems.
     *
     * @param latestFrame A copy of the most recent FrameData received from the NatNet server.
     */
    void framesUpdated(FrameData latestFrame);

    /**
     * @brief Signal emitted when updated asset maps are available.
     *
     * This signal is emitted to provide updated mappings between IDs and names
     * for rigid bodies, skeletons, and bones.
     *
     * @param rigidBodiesMap Map of rigid body ID to name.
     * @param skeletonMap Map of skeleton ID to name.
     * @param boneMap Nested map of skeleton ID to a map of bone ID to bone name.
     */
    void sendMaps(const std::unordered_map<int, std::string> rigidBodiesMap,
                  const std::unordered_map<int, std::string> skeletonMap,
                  const std::unordered_map<int, std::unordered_map<int, std::string>> boneMap);

    /**
     * @brief Signal emitted when the connection status changes.
     *
     * @param connectionStatus True if connected, false otherwise.
     */  
    void connectionStatus(bool connectionStatus);
};