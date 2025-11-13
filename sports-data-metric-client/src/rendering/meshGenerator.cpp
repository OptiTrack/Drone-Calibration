// MeshGenerator.cpp

#include "MeshGenerator.h"
#include "Mesh.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>

void MeshGenerator::cylinder(Mesh& mesh, int segments) {
    std::vector<QVector3D> pos, norm;
    std::vector<uint32_t>  idx;
    generateCylinder(segments, pos, norm, idx);
    setupMesh(pos, norm, idx, mesh);
}

void MeshGenerator::sphere(Mesh& mesh, int stacks, int slices) {
    std::vector<QVector3D> pos, norm;
    std::vector<uint32_t>  idx;
    generateSphere(stacks, slices, pos, norm, idx);
    setupMesh(pos, norm, idx, mesh);
}

void MeshGenerator::wireframe(Mesh& mesh,
                          const std::vector<QVector3D>&    points,
                          const std::vector<uint32_t>&     lineIndices) 
{
    std::vector<QVector3D> dummyNormals(points.size(), QVector3D(0.0f, 1.0f, 0.0f));
    setupMesh(points, dummyNormals, lineIndices, mesh);
}

void MeshGenerator::generateCylinder(int segments,
                                     std::vector<QVector3D>& outPos,
                                     std::vector<QVector3D>& outNorm,
                                     std::vector<uint32_t>&  outIdx)
{
    outPos.clear(); outNorm.clear(); outIdx.clear();
    // side vertices
    for (int i = 0; i < segments; ++i) {
        float theta = 2.0f * M_PI * float(i) / segments;
        float x = std::cos(theta);
        float z = std::sin(theta);
        outPos.emplace_back(x, -0.5f, z);
        outPos.emplace_back(x, +0.5f, z);
        outNorm.emplace_back(x, 0.0f, z);
        outNorm.emplace_back(x, 0.0f, z);
    }
    // side indices (two triangles per segment)
    for (int i = 0; i < segments; ++i) {
        int i0 = i*2;
        int i1 = i*2+1;
        int in = ((i+1)%segments)*2;
        int in1 = in+1;
        // triangle 1
        outIdx.push_back(i0);
        outIdx.push_back(in);
        outIdx.push_back(i1);
        // triangle 2
        outIdx.push_back(i1);
        outIdx.push_back(in);
        outIdx.push_back(in1);
    }
}

void MeshGenerator::generateSphere(int stacks, int slices,
                                   std::vector<QVector3D>& outPos,
                                   std::vector<QVector3D>& outNorm,
                                   std::vector<uint32_t>&  outIdx)
{
    outPos.clear(); outNorm.clear(); outIdx.clear();
    // vertices
    for (int s = 0; s <= stacks; ++s) {
        float phi = M_PI * float(s) / stacks;
        float y = std::cos(phi);
        float r = std::sin(phi);
        for (int i = 0; i <= slices; ++i) {
            float theta = 2.0f * M_PI * float(i) / slices;
            float x = r * std::cos(theta);
            float z = r * std::sin(theta);
            outPos.emplace_back(x, y, z);
            outNorm.emplace_back(x, y, z);  // unit sphere
        }
    }
    // indices (two triangles per quad)
    for (int s = 0; s < stacks; ++s) {
        for (int i = 0; i < slices; ++i) {
            int a = s * (slices+1) + i;
            int b = (s+1) * (slices+1) + i;
            outIdx.push_back(a);
            outIdx.push_back(b);
            outIdx.push_back(a+1);
            outIdx.push_back(a+1);
            outIdx.push_back(b);
            outIdx.push_back(b+1);
        }
    }
}

void MeshGenerator::generateWireframe(std::vector<QVector3D>& outPos,
                                   std::vector<QVector3D>& outNorm,
                                   std::vector<uint32_t>&  outIdx)
{

}

void MeshGenerator::setupMesh(const std::vector<QVector3D>& pos,
                              const std::vector<QVector3D>& norm,
                              const std::vector<uint32_t>&  idx,
                              Mesh&                         mesh)
{
    // interleave pos + normal
    std::vector<float> data;
    data.reserve(pos.size() * 6);
    for (size_t i = 0; i < pos.size(); ++i) {
        const auto &P = pos[i], &N = norm[i];
        data.push_back(P.x()); data.push_back(P.y()); data.push_back(P.z());
        data.push_back(N.x()); data.push_back(N.y()); data.push_back(N.z());
    }
    mesh.setIndexCount(int(idx.size()));

    // upload into VAO/VBO/IBO
    auto *fns = QOpenGLContext::currentContext()->functions();

    // Only create the VAO if it hasn’t been created already:
    if (!mesh.vao().isCreated()) {
        mesh.vao().create();
    }
    mesh.vao().bind();

    mesh.vbo().create();
    mesh.vbo().bind();
    mesh.vbo().allocate(data.data(), int(data.size() * sizeof(float)));

    mesh.ibo().create();
    mesh.ibo().bind();
    mesh.ibo().allocate(idx.data(), int(idx.size() * sizeof(uint32_t)));

    // configure vertex attributes
    fns->glEnableVertexAttribArray(0);
    fns->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                               6 * sizeof(float), reinterpret_cast<void*>(0));
    fns->glEnableVertexAttribArray(1);
    fns->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                               6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    mesh.vao().release();
    mesh.vbo().release();
    mesh.ibo().release();
}
