// MeshGenerator.h

#pragma once

#include <vector>
#include <QVector3D>

class Mesh;

/**
 * @brief Generator function to load a mesh with Vertices/Indices for a shape
 */
class MeshGenerator {
public:
    /**
     * @brief Generate a unit cylinder (height = 1 along +Y, radius = 1) with 'segments' slices.
     * 
     * @param mesh Mesh object to load with data
     * @param segments The number of subdivisions along the x axis 
     */
    static void cylinder(Mesh& mesh, int segments = 16);

    /**
     * @brief Generate a unit sphere (radius = 1) with 'stacks' and 'slices'.
     * 
     * @param mesh Mesh object to load with data
     * @param stacks The number of subdivisions along the y axis
     * @param slices The number of subdivisions along the x axis
     */
    static void sphere(Mesh& mesh, int stacks = 12, int slices = 12);

    static void wireframe(Mesh& mesh,
                          const std::vector<QVector3D>&    points,
                          const std::vector<uint32_t>&     lineIndices);

private:
    /**
     * @brief Generate raw position, normal, and index lists for a cylinder
     * 
     * @param segments The number of subdivisions along the x axis 
     * @param outPos The position vectors for each vertex
     * @param outNorm The normals for each vertex
     * @param outIdx The indices of each face
     */
    static void generateCylinder(int segments,
                                 std::vector<QVector3D>& outPos,
                                 std::vector<QVector3D>& outNorm,
                                 std::vector<uint32_t>&  outIdx);

    /**
     * @brief Generate raw position, normal, and index lists for a sphere
     * 
     * @param stacks The number of subdivisions along the y axis
     * @param slices The number of subdivisions along the x axis
     * @param outPos The position vectors for each vertex
     * @param outNorm The normals for each vertex
     * @param outIdx The indices of each face
     */
    static void generateSphere(int stacks, int slices,
                               std::vector<QVector3D>& outPos,
                               std::vector<QVector3D>& outNorm,
                               std::vector<uint32_t>&  outIdx);

    static void generateWireframe(std::vector<QVector3D>& outPos,
                               std::vector<QVector3D>& outNorm,
                               std::vector<uint32_t>&  outIdx);

    /**
     * @brief Interleave pos+norm into a single VBO, upload indices to IBO, configure VAO.
     * 
     * @param pos The position vectors for each vertex
     * @param norm The normals for each vertex
     * @param idx The indices of each face
     * @param mesh The Mesh to load with data
     */
    static void setupMesh(const std::vector<QVector3D>& pos,
                          const std::vector<QVector3D>& norm,
                          const std::vector<uint32_t>&  idx,
                          Mesh&                         mesh);
};
