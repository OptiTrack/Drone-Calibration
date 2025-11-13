// Adapted from NatNet SDK MinimalClient.cpp
// Original code © NaturalPoint, Inc. (OptiTrack)

// using STL for cross platform sleep
#include <thread>

// NatNet SDK includes
#include "NatNetTypes.h"
#include "NatNetCAPI.h"
#include "NatNetClient.h"

void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData);    // receives data from the server
void PrintData(sFrameOfMocapData* data, NatNetClient* pClient);
void PrintDataDescriptions(sDataDescriptions* pDataDefs);


NatNetClient* g_pClient = nullptr;
sNatNetClientConnectParams g_connectParams;
sServerDescription g_serverDescription;
sDataDescriptions* g_pDataDefs = nullptr;

#include "frame_data.h"
#include "natnet_connection.h"
#include <iostream>

bool NatNetConnection::connect() {
    ErrorCode ret = ErrorCode_OK;

    // Create a NatNet client
    g_pClient = new NatNetClient();

    // Set the Client's frame callback handler
    ret = g_pClient->SetFrameReceivedCallback(DataHandler, this);	

    // Specify client PC's IP address, Motive PC's IP address, and network connection type
    std::string clientIPStr = m_clientIP.toStdString();
    g_connectParams.localAddress = clientIPStr.c_str();

    std::string serverIPStr = m_serverIP.toStdString();
    g_connectParams.serverAddress = serverIPStr.c_str();

    g_connectParams.connectionType = m_connectionType;

    // Connect to Motive
    ret = g_pClient->Connect(g_connectParams);

    if (ret != ErrorCode_OK)
    {
            qInfo() << "Unable to connect to server.  Error code:" << ret << ". Exiting.\n";
            connected = false;
            return 1;
    }
     
    // Get Motive server description
    memset(&g_serverDescription, 0, sizeof(g_serverDescription));
    ret = g_pClient->GetServerDescription(&g_serverDescription);
    if (ret != ErrorCode_OK || !g_serverDescription.HostPresent)
    {
        printf("Unable to get server description. Error Code:%d.  Exiting.\n", ret);
        fflush(stdout);
        return 1;
    }
    else
    {
        printf("Connected : %s (ver. %d.%d.%d.%d)\n", g_serverDescription.szHostApp, g_serverDescription.HostAppVersion[0],
            g_serverDescription.HostAppVersion[1], g_serverDescription.HostAppVersion[2], g_serverDescription.HostAppVersion[3]);

        connected = true;
    }

    // Get current active asset list from Motive
    ret = g_pClient->GetDataDescriptionList(&g_pDataDefs);
    if (ret != ErrorCode_OK || g_pDataDefs == NULL)
    {
        printf("Error getting asset list.  Error Code:%d  Exiting.\n", ret);
        return 1;
    }
    else
    {
        NatNetConnection::processDataDescriptions(g_pDataDefs);
    }
    
    // Start the data acquisition loop in a separate thread
    std::thread dataThread(&NatNetConnection::getData, this);
    dataThread.detach();

    return ret;
}

bool NatNetConnection::disconnect() {
    std::cout << "Disconnecting..." << std::endl;
    connected = false;

        // Clean up
        if (g_pClient)
        {
            g_pClient->Disconnect();
            delete g_pClient;
        }
        
        if (g_pDataDefs)
        {
            NatNet_FreeDescriptions(g_pDataDefs);
            g_pDataDefs = NULL;
        }
    
    return ErrorCode_OK;
}

void NatNetConnection::getData() {
    if (connected) {
        std::cout << "Getting data..." << std::endl;

        // do something on the main app's thread...
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }


    } else {
        std::cout << "Not connected." << std::endl;
    }
}

/**
 * DataHandler called by NatNet on a separate network processing
 * thread whenever a frame of mocap data is available.
 * So at 100 mocap fps, this function should be called ~ every 10ms.
 * \brief DataHandler called by NatNet
 * \param data Input Frame of Mocap data
 * \param pUserData
 * \return 
 */
void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData)
{
  //  NatNetClient* pClient = (NatNetClient*)pUserData;

    NatNetConnection* connection = static_cast<NatNetConnection*>(pUserData);
    connection->processFrameData(data);

    return;
}

void NatNetConnection::processDataDescriptions(sDataDescriptions* pDataDefs)
{
    printf("Retrieved %d Data Descriptions:\n", pDataDefs->nDataDescriptions);

    for (int i = 0; i < pDataDefs->nDataDescriptions; i++)
    {
        printf("---------------------------------\n");
        printf("Data Description # %d (type=%d)\n", i, pDataDefs->arrDataDescriptions[i].type);

        if (pDataDefs->arrDataDescriptions[i].type == Descriptor_RigidBody)
        {
            // RigidBody
            sRigidBodyDescription* pRB = pDataDefs->arrDataDescriptions[i].Data.RigidBodyDescription;
            printf("RigidBody Name : %s\n", pRB->szName);
            printf("RigidBody ID : %d\n", pRB->ID);

            // Save to rigid body name map
            rigidBodyIdToName[pRB->ID] = pRB->szName;
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Skeleton)
        {
            // Skeleton
            sSkeletonDescription* pSK = pDataDefs->arrDataDescriptions[i].Data.SkeletonDescription;
            printf("Skeleton Name : %s\n", pSK->szName);
            printf("Skeleton ID : %d\n", pSK->skeletonID);

            // Save to skeleton name map
            skeletonIdToName[pSK->skeletonID] = pSK->szName;

            // Save each bone under this skeleton
            for (int j = 0; j < pSK->nRigidBodies; j++)
            {
                sRigidBodyDescription* pRB = &pSK->RigidBodies[j];
                printf("  RigidBody Name : %s\n", pRB->szName);
                printf("  RigidBody ID : %d\n", pRB->ID);

                // Save to bone name nested map
                boneIdToName[pSK->skeletonID][pRB->ID] = pRB->szName;
            }
        }
        else
        {
            // All other unused data rtpes
            printf("Unused data type.\n");
        }
    }

    // Invokes callback signal when new frames are available
    if (assetCallback) {
        assetCallback(); 
    }
}

sDataDescriptions* NatNetConnection::getDataDescriptions()
{
    return g_pDataDefs;
}

void NatNetConnection::processFrameData(sFrameOfMocapData* data)
{
    FrameData frame;
    frame.frameNumber = data->iFrame;
    frame.timestamp = data->fTimestamp;

    if (frames.size() > 0 && data->iFrame == frames[frames.size() - 1].frameNumber) {
        return;
    }

    // Parse rigid bodies data
    for (int i = 0; i < data->nRigidBodies; i++)
    {
        // Extract rigid body data from NatNet data
        const sRigidBodyData& rb = data->RigidBodies[i];

        // Create rigid body struct
        RigidBodyData rbData;
        rbData.id = rb.ID;
        rbData.position = QVector3D(rb.x, rb.y, rb.z);
        rbData.orientation = QQuaternion(rb.qw, rb.qx, rb.qy, rb.qz);

        // Append rigid body
        frame.rigidBodies.push_back(rbData);
    }

    // Parse skeletons data
    for (int i = 0; i < data->nSkeletons; i++)
    {
        // Extract skeleton data from NatNet data
        const sSkeletonData& skel = data->Skeletons[i];

        // Create skeleton struct
        SkeletonData skelData;
        skelData.id = skel.skeletonID;

        // Parse bones (rigid bodies inside skel)
        for (int j = 0; j < skel.nRigidBodies; j++)
        {
            // Extract bones data from NatNet data
            const sRigidBodyData& bone = skel.RigidBodyData[j];

            // Create bone rigid body struct
            RigidBodyData boneData;
            boneData.id = bone.ID;
            // boneData.parentId = bone.
            boneData.position = QVector3D(bone.x, bone.y, bone.z);
            boneData.orientation = QQuaternion(bone.qw, bone.qx, bone.qy, bone.qz);

            // Append bone
            skelData.bones.push_back(boneData);
        }
        // Append skeleton 
        frame.skeletons.push_back(skelData);
    }

    // Append frame
    {
        QMutexLocker locker(&frameMutex);
        frames.push_back(frame);
    }

    // Invokes callback signal when new frames are available
    if (frameCallback) {
        frameCallback(); 
    }
}

const std::vector<FrameData>& NatNetConnection::getFrames() const
{
    return frames;
}

FrameData NatNetConnection::getLatestFrame() const {
    QMutexLocker locker(&frameMutex);
    return frames.empty() ? FrameData{} : frames.back();
}

const std::unordered_map<int, std::string>& NatNetConnection::getRigidBodyIdToName() const
{
    return rigidBodyIdToName;
}

const std::unordered_map<int, std::string>& NatNetConnection::getSkeletonIdToName() const
{
    return skeletonIdToName;
}

const std::unordered_map<int, std::unordered_map<int, std::string>>& NatNetConnection::getBoneIdToName() const
{
    return boneIdToName;
}

void NatNetConnection::setFrameUpdateCallback(std::function<void()> callback) 
{
    frameCallback = std::move(callback);
}

void NatNetConnection::setAssetUpdateCallback(std::function<void()> callback) 
{
    assetCallback = std::move(callback);
}

void NatNetConnection::setServerIP(const QString& ip) 
{
    m_serverIP = ip;
}

void NatNetConnection::setClientIP(const QString& ip) 
{
    m_clientIP = ip;
}

void NatNetConnection::setConnectionType(QString& type) 
{
    if (type == "Multicast")
    {
        m_connectionType = ConnectionType_Multicast;
    } else if (type == "Unicast")
    {
        m_connectionType = ConnectionType_Unicast;
    }
}

void NatNetConnection::setNamingConvention(const QString& convention) 
{
    m_namingConvention = convention;
}

bool NatNetConnection::getConnectionStatus()
{
    return connected;
}