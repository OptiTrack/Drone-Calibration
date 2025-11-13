// Mesh.cpp

#include "Mesh.h"

Mesh::Mesh()
    : m_vbo(QOpenGLBuffer::VertexBuffer),
      m_ibo(QOpenGLBuffer::IndexBuffer),
      m_indexCount(0)
{
}

Mesh::~Mesh()
{
    // Clean up GPU resources
    if (m_vao.isCreated())
        m_vao.destroy();
    if (m_vbo.isCreated())
        m_vbo.destroy();
    if (m_ibo.isCreated())
        m_ibo.destroy();
}

void Mesh::clear() {
    if (m_vao.isCreated()) m_vao.destroy();
    if (m_vbo.isCreated()) m_vbo.destroy();
    if (m_ibo.isCreated()) m_ibo.destroy();
    m_indexCount = 0;
}

QOpenGLVertexArrayObject& Mesh::vao() { 
    return m_vao; 
}

QOpenGLBuffer& Mesh::vbo() { 
    return m_vbo; 
}

QOpenGLBuffer& Mesh::ibo() { 
    return m_ibo; 
}

int Mesh::id() {
    return m_id;
}


int Mesh::indexCount() const { 
    return m_indexCount; 
}

void Mesh::setIndexCount(int count) { 
    m_indexCount = count; 
}

void Mesh::setIdAndType(int id, QString type) {
    m_id = id;
    m_type = type;
}
