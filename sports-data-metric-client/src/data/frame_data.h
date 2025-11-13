// Defines data structs for representing motion capture data received from the NatNet client.
// 
// Structs:
// - RigidBodyData: Represents a single rigid body's position, orientation, and tracking state.
// - SkeletonData: Represents a skeleton composed of multiple rigid bodies (bones).
// - FrameData: Represents a full frame of motion capture data, containing all rigid bodies and skeletons.
// 
// These structures are used for parsing, organizing, and accessing
// real-time motion capture data streamed over the network.

#pragma once

#include <vector>
#include <string>
#include <QVector3D>
#include <QQuaternion>

struct RigidBodyData {
    int id = -1;                    // Motive Rigid body ID
    int parentId = -1;              // ID of parent rigid body
    QVector3D position;             // X, Y, Z position
    QQuaternion orientation;        // Quaternion orientation (X, Y, Z, W)
};

struct SkeletonData {
    int id = -1;                              // Motive skeleton ID
    std::vector<RigidBodyData> bones;         // Array of RigidBody bones in skeleton
};

struct FrameData {
    int frameNumber = 0;                        // Frame number from Motive
    double timestamp = 0;
    std::vector<RigidBodyData> rigidBodies;     // Array of rigid bodies
    std::vector<SkeletonData> skeletons;        // Array of skeletons
};
