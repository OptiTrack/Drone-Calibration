// Mesh.h

#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>

/// Simple wrapper for a mesh’s GPU buffers and index count.
class Mesh {
public:
    Mesh();
    ~Mesh();

    void clear();

    /// Getters
    QOpenGLVertexArrayObject& vao();

    QOpenGLBuffer& vbo();

    QOpenGLBuffer& ibo();

    int id();

    int indexCount() const;

    /// Setters
    void setVao(const QOpenGLVertexArrayObject& vao);
    void setVbo(const QOpenGLBuffer& vbo);
    void setIbo(const QOpenGLBuffer& ibo);
    void setIndexCount(int count);
    void setIdAndType(int id, QString type);

private:
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer           m_vbo{ QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer           m_ibo{ QOpenGLBuffer::IndexBuffer };
    QString                 m_type;
    int                     m_id;
    int                     m_indexCount = 0;
};
