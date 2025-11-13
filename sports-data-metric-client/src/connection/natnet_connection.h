// Manages connection to the NatNet server, data reception, and storage
// of motion capture frames, rigid body, and skeleton names.

#pragma once

#include "frame_data.h"
#include <vector>
#include <unordered_map>
#include <string>
#include "NatNetTypes.h"
#include <functional>
#include <QMutex>

class NatNetConnection {
public:
    /**
     * @brief Establishes a connection to the NatNet server.
     * @return True if connection succeeds, false otherwise.
     */
    bool connect();

    /**
     * @brief Disconnects from the NatNet server and cleans up resources.
     * @return True if disconnection succeeds, false otherwise.
     */
    bool disconnect();

    /**
     * @brief Processes skeleton and rigid body data from a new motion capture frame.
     * @param data Pointer to the received frame data.
     */
    void processFrameData(sFrameOfMocapData* data);

    /**
     * @brief Gets the list of captured motion frames.
     * @return A constant reference to the vector of FrameData.
     */
    const std::vector<FrameData>& getFrames() const;

    /**
     * @brief Retrieves a thread-safe copy of the latest frame.
     * @return A copy of the most recent FrameData.
     */
    FrameData getLatestFrame() const;

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
     * @brief Gets the mapping from skeleton and bone IDs to their corresponding names.
     * @return A constant reference to the nested skeleton-to-bone ID-to-name map.
     */
    const std::unordered_map<int, std::unordered_map<int, std::string>>& getBoneIdToName() const;

    /**
     * @brief Sets the callback function to be called when new frame data is received.
     * @param callback A function with no arguments and no return value to execute when frames update.
     */
    void setFrameUpdateCallback(std::function<void()> callback);

    /**
     * @brief Sets the callback function to be called when new asset map is received.
     * @param callback A function with no arguments and no return value to execute when new assets update.
     */
    void setAssetUpdateCallback(std::function<void()> callback);

    /**
     * @brief Sets the IP address of the NatNet server.
     * 
     * This IP will be used when establishing the connection.
     * @param ip The server's IP address as a QString.
     */
    void setServerIP(const QString& ip);

    /**
     * @brief Sets the IP address of the local client.
     * 
     * This IP is used for the local network interface to receive data from the server.
     * @param ip The client's IP address as a QString.
     */
    void setClientIP(const QString& ip);

    /**
     * @brief Sets the connection type (Unicast or Multicast).
     * 
     * Determines how NatNet will stream data to this client.
     * @param type A value from the ConnectionType enum.
     */
    void setConnectionType(QString& type);

    /**
     * @brief Sets the naming convention to use for bones, skeletons, etc.
     * 
     * This can be used to support custom naming styles.
     * @param convention A QString identifier for the naming style.
     */
    void setNamingConvention(const QString& convention);

    /**
     * @brief Gets the data descriptions of the rigid bodies in the scene.
     * @return The data descriptions object.
     */
    sDataDescriptions* getDataDescriptions();

    /**
     * @brief Checks the current connection status to the NatNet server.
     * @return True if connected, false otherwise.
     */
    bool getConnectionStatus();

private:
    bool connected = false;                      // Indicates whether the connection is currently active

    QString m_serverIP = "127.0.0.1";                               // IP address of the NatNet server
    QString m_clientIP = "127.0.0.1";                               // IP address of the client (local machine)
    ConnectionType m_connectionType = ConnectionType_Multicast;     // Network mode for receiving data
    QString m_namingConvention = "default";                         // Naming convention for asset names

    /**
     * @brief Starts receiving and parsing incoming data frames from the server.
     */
    void getData();

    /**
     * @brief Populates ID-to-name maps from the server's data descriptions.
     * @param pDataDefs Pointer to the NatNet sDataDescriptions structure.
     */
    void processDataDescriptions(sDataDescriptions* pDataDefs);

    mutable QMutex frameMutex;      // Protects access to frames for thread safety
    std::vector<FrameData> frames;  // Stored motion capture frames

    std::unordered_map<int, std::string> rigidBodyIdToName;                     // Map rigid body ID -> name
    std::unordered_map<int, std::string> skeletonIdToName;                      // Map skeleton ID -> skeleton name
    std::unordered_map<int, std::unordered_map<int, std::string>> boneIdToName;  // Map bone ID -> name within each skeleton

    std::function<void()> frameCallback;  // store the callback function for frames signal
    std::function<void()> assetCallback;  // store the callback function for assets signal
};
