#include "pathplannerwidget.h"
#include "../controllers/dronecontroller.h"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QListWidgetItem>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>
#include <QtMath>
#include <QPainter>
#include <algorithm>

// Helper function to convert waypoint ID to letter label (1=A, 2=B, etc.)
static QString idToLetter(int id)
{
    if (id < 1) return "?";
    if (id <= 26) return QString(QChar('A' + id - 1));
    // For more than 26 waypoints: AA, AB, AC, etc.
    int first = (id - 1) / 26;
    int second = (id - 1) % 26;
    if (first > 0 && first <= 26)
        return QString(QChar('A' + first - 1)) + QString(QChar('A' + second));
    return QString::number(id);
}

// Vertex shader source
static const char *vertexShaderSource =
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aColor;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 projection;\n"
    "out vec3 FragColor;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = projection * view * model * vec4(aPos, 1.0);\n"
    "   FragColor = aColor;\n"
    "}\0";

// Fragment shader source
static const char *fragmentShaderSource =
    "#version 330 core\n"
    "in vec3 FragColor;\n"
    "out vec4 color;\n"
    "void main()\n"
    "{\n"
    "   color = vec4(FragColor, 1.0);\n"
    "}\n\0";

// PathPlannerOpenGLWidget Implementation
PathPlannerOpenGLWidget::PathPlannerOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent), m_shaderProgram(nullptr), m_cameraPosition(0, 5, 10), m_cameraTarget(0, 0, 0), m_cameraUp(0, 1, 0), m_cameraDistance(15.0f), m_cameraYaw(0.0f), m_cameraPitch(30.0f), m_viewMode(View3DMode), m_orthoZoom(10.0f), m_defaultAltitude(2.0f), m_selectedWaypoint(-1), m_mousePressed(false), m_isDragging(false), m_animationTime(0.0f)
{
    setMinimumSize(600, 400);
    setFocusPolicy(Qt::StrongFocus);

    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(16); // ~60 FPS
    connect(m_animationTimer, &QTimer::timeout, [this]()
            {
        m_animationTime += 0.016f;
        update(); });
    m_animationTimer->start();
}

PathPlannerOpenGLWidget::~PathPlannerOpenGLWidget()
{
    makeCurrent();
    delete m_shaderProgram;
    doneCurrent();
}

void PathPlannerOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Note: GL_POINT_SMOOTH and GL_LINE_SMOOTH are deprecated in OpenGL core profile
    // and not supported on macOS. Multisampling is enabled via QSurfaceFormat instead.

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    setupShaders();
    setupBuffers();
}

void PathPlannerOpenGLWidget::setupShaders()
{
    m_shaderProgram = new QOpenGLShaderProgram(this);
    
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        qWarning() << "PathPlanner: Failed to compile vertex shader:" << m_shaderProgram->log();
    }
    
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
        qWarning() << "PathPlanner: Failed to compile fragment shader:" << m_shaderProgram->log();
    }
    
    if (!m_shaderProgram->link()) {
        qWarning() << "PathPlanner: Failed to link shader program:" << m_shaderProgram->log();
    }
}

void PathPlannerOpenGLWidget::setupBuffers()
{
    m_vao.create();
    m_vertexBuffer.create();
    m_indexBuffer.create();
}

void PathPlannerOpenGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    updateCamera();

    m_shaderProgram->bind();
    m_shaderProgram->setUniformValue("projection", m_projectionMatrix);
    m_shaderProgram->setUniformValue("view", m_viewMatrix);
    m_shaderProgram->setUniformValue("model", m_modelMatrix);

    drawGrid();
    drawAxes();
    drawPath();
    drawWaypoints();

    m_shaderProgram->release();
}

void PathPlannerOpenGLWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, width, height);
    updateProjection();
}

void PathPlannerOpenGLWidget::paintEvent(QPaintEvent *event)
{
    // First, do the OpenGL rendering
    QOpenGLWidget::paintEvent(event);
    
    // Then draw 2D overlays using QPainter
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawWaypointLabels(painter);
    painter.end();
}

QPoint PathPlannerOpenGLWidget::worldToScreen(const QVector3D &worldPos)
{
    // Project 3D world position to 2D screen coordinates
    QVector4D clipPos = m_projectionMatrix * m_viewMatrix * QVector4D(worldPos, 1.0f);
    if (qAbs(clipPos.w()) < 0.0001f)
        return QPoint(-1000, -1000);  // Behind camera
    
    QVector3D ndcPos = clipPos.toVector3D() / clipPos.w();
    
    int screenX = static_cast<int>((ndcPos.x() + 1.0f) * 0.5f * width());
    int screenY = static_cast<int>((1.0f - ndcPos.y()) * 0.5f * height());
    
    return QPoint(screenX, screenY);
}

void PathPlannerOpenGLWidget::drawWaypointLabels(QPainter &painter)
{
    if (m_waypoints.empty())
        return;
    
    // Set up font for labels
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(12);
    painter.setFont(font);
    
    for (size_t i = 0; i < m_waypoints.size(); ++i)
    {
        const Waypoint &wp = m_waypoints[i];
        QVector3D worldPos(wp.x(), wp.y(), wp.z());
        QPoint screenPos = worldToScreen(worldPos);
        
        // Skip if off-screen
        if (screenPos.x() < -50 || screenPos.x() > width() + 50 ||
            screenPos.y() < -50 || screenPos.y() > height() + 50)
            continue;
        
        // Get the letter label
        QString label = idToLetter(wp.sequence());
        
        // Draw background circle
        int radius = 14;
        QPoint labelPos = screenPos + QPoint(radius + 5, -radius - 5);  // Offset from sphere
        
        // Choose colors based on selection
        QColor bgColor = (wp.sequence() == m_selectedWaypoint) ? QColor(51, 153, 255) : QColor(51, 204, 51);
        QColor textColor = Qt::white;
        
        // Draw circle background
        painter.setBrush(bgColor);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawEllipse(labelPos, radius, radius);
        
        // Draw label text
        painter.setPen(textColor);
        QRect textRect(labelPos.x() - radius, labelPos.y() - radius, radius * 2, radius * 2);
        painter.drawText(textRect, Qt::AlignCenter, label);
    }
}

void PathPlannerOpenGLWidget::updateProjection()
{
    float aspect = float(width()) / float(height());
    m_projectionMatrix.setToIdentity();

    if (m_viewMode == TopDownMode)
    {
        // Orthographic projection for top-down view
        float halfWidth = m_orthoZoom * aspect;
        float halfHeight = m_orthoZoom;
        m_projectionMatrix.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, 0.1f, 100.0f);
    }
    else
    {
        // Perspective projection for 3D view
        m_projectionMatrix.perspective(45.0f, aspect, 0.1f, 100.0f);
    }
}

void PathPlannerOpenGLWidget::updateCamera()
{
    m_viewMatrix.setToIdentity();

    if (m_viewMode == TopDownMode)
    {
        // Top-down orthographic view: camera looking straight down
        // Position camera above the target looking down
        // Using standard orientation: +Z points up on screen, +X points right
        m_cameraPosition = m_cameraTarget + QVector3D(0, 20, 0);
        m_cameraUp = QVector3D(0, 0, 1); // +Z points up on screen in top-down view
        m_viewMatrix.lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);
    }
    else
    {
        // 3D view: spherical coordinate camera
        float x = m_cameraDistance * cos(qDegreesToRadians(m_cameraPitch)) * cos(qDegreesToRadians(m_cameraYaw));
        float y = m_cameraDistance * sin(qDegreesToRadians(m_cameraPitch));
        float z = m_cameraDistance * cos(qDegreesToRadians(m_cameraPitch)) * sin(qDegreesToRadians(m_cameraYaw));

        m_cameraPosition = QVector3D(x, y, z) + m_cameraTarget;
        m_cameraUp = QVector3D(0, 1, 0);
        m_viewMatrix.lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);
    }
}

void PathPlannerOpenGLWidget::drawGrid()
{
    QVector<float> gridVertices;
    QVector<float> gridColors;

    float gridSize = 20.0f;
    float gridSpacing = 1.0f;

    // Grid lines parallel to X axis
    for (float z = -gridSize; z <= gridSize; z += gridSpacing)
    {
        gridVertices << -gridSize << 0.0f << z;
        gridVertices << gridSize << 0.0f << z;

        float alpha = (z == 0.0f) ? 0.8f : 0.3f;
        gridColors << 0.5f << 0.5f << 0.5f;
        gridColors << 0.5f << 0.5f << 0.5f;
    }

    // Grid lines parallel to Z axis
    for (float x = -gridSize; x <= gridSize; x += gridSpacing)
    {
        gridVertices << x << 0.0f << -gridSize;
        gridVertices << x << 0.0f << gridSize;

        float alpha = (x == 0.0f) ? 0.8f : 0.3f;
        gridColors << 0.5f << 0.5f << 0.5f;
        gridColors << 0.5f << 0.5f << 0.5f;
    }

    if (!gridVertices.isEmpty())
    {
        m_vao.bind();
        m_vertexBuffer.bind();
        m_vertexBuffer.allocate(gridVertices.data(), gridVertices.size() * sizeof(float));

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        // Temporarily bind color data
        QOpenGLBuffer colorBuffer;
        colorBuffer.create();
        colorBuffer.bind();
        colorBuffer.allocate(gridColors.data(), gridColors.size() * sizeof(float));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);

        glDrawArrays(GL_LINES, 0, gridVertices.size() / 3);

        colorBuffer.release();
        m_vertexBuffer.release();
        m_vao.release();
    }
}

// Helper function to generate cylinder vertices for thick axis lines
static void generateCylinderVertices(const QVector3D &start, const QVector3D &end, float radius, 
                                      const QVector3D &color, QVector<float> &vertices, QVector<float> &colors)
{
    const int segments = 8;
    QVector3D dir = (end - start).normalized();
    
    // Find perpendicular vectors
    QVector3D perp1 = QVector3D::crossProduct(dir, QVector3D(0, 1, 0));
    if (perp1.length() < 0.001f)
        perp1 = QVector3D::crossProduct(dir, QVector3D(1, 0, 0));
    perp1.normalize();
    QVector3D perp2 = QVector3D::crossProduct(dir, perp1).normalized();
    
    // Generate triangles for the cylinder
    for (int i = 0; i < segments; ++i)
    {
        float angle1 = (2.0f * M_PI * i) / segments;
        float angle2 = (2.0f * M_PI * (i + 1)) / segments;
        
        QVector3D offset1 = (perp1 * cos(angle1) + perp2 * sin(angle1)) * radius;
        QVector3D offset2 = (perp1 * cos(angle2) + perp2 * sin(angle2)) * radius;
        
        QVector3D p1 = start + offset1;
        QVector3D p2 = start + offset2;
        QVector3D p3 = end + offset1;
        QVector3D p4 = end + offset2;
        
        // Triangle 1
        vertices << p1.x() << p1.y() << p1.z();
        vertices << p2.x() << p2.y() << p2.z();
        vertices << p3.x() << p3.y() << p3.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
        
        // Triangle 2
        vertices << p2.x() << p2.y() << p2.z();
        vertices << p4.x() << p4.y() << p4.z();
        vertices << p3.x() << p3.y() << p3.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
    }
}

void PathPlannerOpenGLWidget::drawAxes()
{
    QVector<float> axesVertices;
    QVector<float> axesColors;
    
    float axisLength = 2.0f;
    float axisRadius = 0.05f;
    
    // X axis (red) - cylinder from origin to (axisLength, 0, 0)
    generateCylinderVertices(QVector3D(0, 0, 0), QVector3D(axisLength, 0, 0), axisRadius,
                             QVector3D(1.0f, 0.0f, 0.0f), axesVertices, axesColors);
    
    // Y axis (green) - cylinder from origin to (0, axisLength, 0)
    generateCylinderVertices(QVector3D(0, 0, 0), QVector3D(0, axisLength, 0), axisRadius,
                             QVector3D(0.0f, 1.0f, 0.0f), axesVertices, axesColors);
    
    // Z axis (blue) - cylinder from origin to (0, 0, axisLength)
    generateCylinderVertices(QVector3D(0, 0, 0), QVector3D(0, 0, axisLength), axisRadius,
                             QVector3D(0.0f, 0.0f, 1.0f), axesVertices, axesColors);

    if (axesVertices.isEmpty())
        return;

    m_vao.bind();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(axesVertices.data(), axesVertices.size() * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    QOpenGLBuffer colorBuffer;
    colorBuffer.create();
    colorBuffer.bind();
    colorBuffer.allocate(axesColors.data(), axesColors.size() * sizeof(float));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, axesVertices.size() / 3);

    colorBuffer.release();
    m_vertexBuffer.release();
    m_vao.release();
}

// Helper function to generate sphere vertices for waypoint balls
static void generateSphereVertices(const QVector3D &center, float radius,
                                    const QVector3D &color, QVector<float> &vertices, QVector<float> &colors)
{
    const int latSegments = 12;
    const int lonSegments = 16;
    
    for (int lat = 0; lat < latSegments; ++lat)
    {
        float theta1 = (M_PI * lat) / latSegments;
        float theta2 = (M_PI * (lat + 1)) / latSegments;
        
        for (int lon = 0; lon < lonSegments; ++lon)
        {
            float phi1 = (2.0f * M_PI * lon) / lonSegments;
            float phi2 = (2.0f * M_PI * (lon + 1)) / lonSegments;
            
            // Four corners of the quad
            QVector3D p1(
                center.x() + radius * sin(theta1) * cos(phi1),
                center.y() + radius * cos(theta1),
                center.z() + radius * sin(theta1) * sin(phi1)
            );
            QVector3D p2(
                center.x() + radius * sin(theta1) * cos(phi2),
                center.y() + radius * cos(theta1),
                center.z() + radius * sin(theta1) * sin(phi2)
            );
            QVector3D p3(
                center.x() + radius * sin(theta2) * cos(phi1),
                center.y() + radius * cos(theta2),
                center.z() + radius * sin(theta2) * sin(phi1)
            );
            QVector3D p4(
                center.x() + radius * sin(theta2) * cos(phi2),
                center.y() + radius * cos(theta2),
                center.z() + radius * sin(theta2) * sin(phi2)
            );
            
            // Triangle 1
            vertices << p1.x() << p1.y() << p1.z();
            vertices << p3.x() << p3.y() << p3.z();
            vertices << p2.x() << p2.y() << p2.z();
            colors << color.x() << color.y() << color.z();
            colors << color.x() << color.y() << color.z();
            colors << color.x() << color.y() << color.z();
            
            // Triangle 2
            vertices << p2.x() << p2.y() << p2.z();
            vertices << p3.x() << p3.y() << p3.z();
            vertices << p4.x() << p4.y() << p4.z();
            colors << color.x() << color.y() << color.z();
            colors << color.x() << color.y() << color.z();
            colors << color.x() << color.y() << color.z();
        }
    }
}

void PathPlannerOpenGLWidget::drawWaypoints()
{
    if (m_waypoints.empty())
        return;

    QVector<float> waypointVertices;
    QVector<float> waypointColors;
    
    float sphereRadius = 0.2f;  // Radius of the waypoint balls

    for (size_t i = 0; i < m_waypoints.size(); ++i)
    {
        const Waypoint &wp = m_waypoints[i];
        QVector3D center(wp.x(), wp.y(), wp.z());
        QVector3D color;

        if (wp.sequence() == m_selectedWaypoint)
        {
            color = QVector3D(0.2f, 0.6f, 1.0f); // Blue for selected
        }
        else
        {
            color = QVector3D(0.2f, 0.8f, 0.2f); // Green for normal
        }
        
        generateSphereVertices(center, sphereRadius, color, waypointVertices, waypointColors);
    }

    if (waypointVertices.isEmpty())
        return;

    m_vao.bind();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(waypointVertices.data(), waypointVertices.size() * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    QOpenGLBuffer colorBuffer;
    colorBuffer.create();
    colorBuffer.bind();
    colorBuffer.allocate(waypointColors.data(), waypointColors.size() * sizeof(float));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, waypointVertices.size() / 3);

    colorBuffer.release();
    m_vertexBuffer.release();
    m_vao.release();

    // Note: Text rendering would require QPainter overlay or texture-based text
    // For simplicity, we'll rely on the table widget for waypoint identification
}

void PathPlannerOpenGLWidget::drawPath()
{
    if (m_waypoints.size() < 2)
        return;

    QVector<float> pathVertices;
    QVector<float> pathColors;

    for (size_t i = 0; i < m_waypoints.size() - 1; ++i)
    {
        const Waypoint &wp1 = m_waypoints[i];
        const Waypoint &wp2 = m_waypoints[i + 1];

        pathVertices << wp1.x() << wp1.y() << wp1.z();
        pathVertices << wp2.x() << wp2.y() << wp2.z();

        pathColors << 1.0f << 1.0f << 0.0f; // Yellow
        pathColors << 1.0f << 1.0f << 0.0f;
    }

    m_vao.bind();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(pathVertices.data(), pathVertices.size() * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    QOpenGLBuffer colorBuffer;
    colorBuffer.create();
    colorBuffer.bind();
    colorBuffer.allocate(pathColors.data(), pathColors.size() * sizeof(float));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);

    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, pathVertices.size() / 3);
    glLineWidth(1.0f);

    colorBuffer.release();
    m_vertexBuffer.release();
    m_vao.release();
}

void PathPlannerOpenGLWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePos = event->pos();
    m_mousePressed = true;

    if (event->button() == Qt::LeftButton)
    {
        // In top-down mode, left-click adds waypoints
        if (m_viewMode == TopDownMode)
        {
            // Place waypoint on ground plane (Y=0) for visual placement
            // The depth parameter is not used in top-down mode, Y is set explicitly
            QVector3D worldPos = screenToWorld(event->pos(), 0.0f);
            worldPos.setY(0.0f); // Place on ground plane, user can adjust altitude in table
            addWaypoint(worldPos);
            // Signal is emitted in addWaypoint
        }
        else
        {
            // In 3D mode, left-click selects waypoints (no adding via mouse)
            int waypointId = findWaypointAt(event->pos());
            if (waypointId >= 0)
            {
                setSelectedWaypoint(waypointId);
                emit waypointSelected(waypointId);
            }
        }
    }
}

void PathPlannerOpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_mousePressed)
        return;

    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    // Only allow camera manipulation in 3D mode
    if (m_viewMode == View3DMode)
    {
        if (event->buttons() & Qt::RightButton)
        {
            // Camera rotation
            m_cameraYaw += delta.x() * 0.5f;
            m_cameraPitch -= delta.y() * 0.5f;
            m_cameraPitch = qBound(-89.0f, m_cameraPitch, 89.0f);
            update();
        }
        else if (event->buttons() & Qt::MiddleButton)
        {
            // Camera panning
            float sensitivity = 0.01f;
            QVector3D right = QVector3D::crossProduct(m_cameraTarget - m_cameraPosition, m_cameraUp).normalized();
            QVector3D up = QVector3D::crossProduct(right, m_cameraTarget - m_cameraPosition).normalized();
            m_cameraTarget += right * delta.x() * sensitivity;
            m_cameraTarget += up * delta.y() * sensitivity;
            update();
        }
    }
    else if (m_viewMode == TopDownMode)
    {
        // In top-down mode, middle button or right button pans the view
        if (event->buttons() & (Qt::RightButton | Qt::MiddleButton))
        {
            float sensitivity = 0.02f * m_orthoZoom;
            // X is flipped in screen-to-world, so flip pan direction too
            m_cameraTarget.setX(m_cameraTarget.x() + delta.x() * sensitivity);
            m_cameraTarget.setZ(m_cameraTarget.z() + delta.y() * sensitivity);
            update();
        }
    }
}

void PathPlannerOpenGLWidget::wheelEvent(QWheelEvent *event)
{
    float delta = event->angleDelta().y() / 120.0f;

    if (m_viewMode == TopDownMode)
    {
        // Zoom in/out in orthographic mode
        m_orthoZoom -= delta * 0.5f;
        m_orthoZoom = qBound(1.0f, m_orthoZoom, 50.0f);
        updateProjection();
    }
    else
    {
        // Zoom in/out in perspective mode
        m_cameraDistance -= delta * 0.5f;
        m_cameraDistance = qBound(2.0f, m_cameraDistance, 50.0f);
    }
    update();
}

QVector3D PathPlannerOpenGLWidget::screenToWorld(const QPoint &screenPos, float depth)
{
    if (m_viewMode == TopDownMode)
    {
        // Orthographic unprojection for top-down view
        // Camera is looking down -Y axis, with +Z up on screen and +X right on screen
        float aspect = float(width()) / float(height());
        float halfWidth = m_orthoZoom * aspect;
        float halfHeight = m_orthoZoom;

        // Normalize screen coordinates to [-1, 1]
        float ndcX = (2.0f * screenPos.x()) / width() - 1.0f;
        float ndcY = 1.0f - (2.0f * screenPos.y()) / height();

        // Map to world coordinates
        // Screen X maps to world -X (need to flip for correct orientation)
        // Screen Y maps to world Z (up on screen is positive Z in world)
        float worldX = -ndcX * halfWidth + m_cameraTarget.x();
        float worldZ = ndcY * halfHeight + m_cameraTarget.z();

        return QVector3D(worldX, depth, worldZ);
    }
    else
    {
        // Simplified perspective unprojection (for ground plane y=depth)
        float x = (screenPos.x() - width() / 2.0f) / (width() / 20.0f);
        float z = (screenPos.y() - height() / 2.0f) / (height() / 20.0f);
        return QVector3D(x, depth, -z);
    }
}

int PathPlannerOpenGLWidget::findWaypointAt(const QPoint &screenPos)
{
    // Simple hit testing - proper implementation would use GPU picking or ray casting
    for (size_t i = 0; i < m_waypoints.size(); ++i)
    {
        const Waypoint &wp = m_waypoints[i];
        QVector3D wpPos(wp.x(), wp.y(), wp.z());

        // Project waypoint to screen (simplified)
        QVector4D clipPos = m_projectionMatrix * m_viewMatrix * QVector4D(wpPos, 1.0f);
        if (clipPos.w() != 0.0f)
        {
            QVector3D ndcPos = clipPos.toVector3D() / clipPos.w();
            QPoint screenPoint(
                (ndcPos.x() + 1.0f) * 0.5f * width(),
                (1.0f - ndcPos.y()) * 0.5f * height());

            if ((screenPoint - screenPos).manhattanLength() < 15)
            {
                return wp.sequence();
            }
        }
    }
    return -1;
}

void PathPlannerOpenGLWidget::setWaypoints(const std::vector<Waypoint> &waypoints)
{
    m_waypoints = waypoints;
    update();
}

void PathPlannerOpenGLWidget::addWaypoint(const QVector3D &point)
{
    // Generate new sequence number (1-based, sequential)
    int newSequence = 1;
    if (!m_waypoints.empty())
    {
        newSequence = m_waypoints.back().sequence() + 1;
    }

    Waypoint wp(point);
    wp.setSequence(newSequence);
    m_waypoints.push_back(wp);
    emit waypointAdded(wp);
    update();
}

void PathPlannerOpenGLWidget::updateWaypoint(int id, const Waypoint &wp)
{
    for (auto &waypoint : m_waypoints)
    {
        if (waypoint.sequence() == id)
        {
            waypoint = wp;
            waypoint.setSequence(id); // Preserve sequence number
            update();
            return;
        }
    }
}

void PathPlannerOpenGLWidget::removeWaypoint(int id)
{
    auto it = std::remove_if(m_waypoints.begin(), m_waypoints.end(),
                             [id](const Waypoint &wp)
                             { return wp.sequence() == id; });

    if (it != m_waypoints.end())
    {
        m_waypoints.erase(it, m_waypoints.end());

        if (m_selectedWaypoint == id)
        {
            m_selectedWaypoint = -1;
        }
        update();
    }
}

void PathPlannerOpenGLWidget::clearWaypoints()
{
    m_waypoints.clear();
    m_selectedWaypoint = -1;
    update();
}

void PathPlannerOpenGLWidget::setSelectedWaypoint(int id)
{
    m_selectedWaypoint = id;
    update();
}

void PathPlannerOpenGLWidget::setViewMode(ViewMode mode)
{
    if (m_viewMode != mode)
    {
        m_viewMode = mode;
        updateProjection();
        update();
    }
}

void PathPlannerOpenGLWidget::resetCamera()
{
    if (m_viewMode == TopDownMode)
    {
        m_cameraTarget = QVector3D(0, 0, 0);
        m_orthoZoom = 10.0f;
        updateProjection();
    }
    else
    {
        m_cameraTarget = QVector3D(0, 0, 0);
        m_cameraDistance = 15.0f;
        m_cameraYaw = 0.0f;
        m_cameraPitch = 30.0f;
    }
    update();
}

// PathPlannerWidget Implementation
PathPlannerWidget::PathPlannerWidget(QWidget *parent)
    : QWidget(parent), ui(nullptr), m_mainLayout(nullptr), m_controlsLayout(nullptr), m_openglWidget(nullptr), m_waypointGroup(nullptr), m_pathGroup(nullptr), m_pathOrderGroup(nullptr), m_viewGroup(nullptr), m_settingsGroup(nullptr), m_waypointTable(nullptr), m_selectedWaypoint(-1), m_currentAnimationWaypoint(0), m_animationProgress(0.0f), m_isPlayingPath(false), m_droneController(nullptr)
{
    setupUI();

    m_pathAnimationTimer = new QTimer(this);
    m_pathAnimationTimer->setInterval(50); // 20 FPS for path animation
    connect(m_pathAnimationTimer, &QTimer::timeout, this, &PathPlannerWidget::onPathAnimationTimer);
}

PathPlannerWidget::~PathPlannerWidget()
{
}

void PathPlannerWidget::setupUI()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);

    // Create OpenGL widget
    m_openglWidget = new PathPlannerOpenGLWidget;
    m_mainLayout->addWidget(m_openglWidget, 3);

    // Create controls panel
    m_controlsLayout = new QVBoxLayout;
    m_mainLayout->addLayout(m_controlsLayout, 1);

    setupControls();
    setupWaypointTable();

    // Connect signals
    connect(m_openglWidget, &PathPlannerOpenGLWidget::waypointSelected,
            this, &PathPlannerWidget::onWaypointSelected);
    connect(m_openglWidget, &PathPlannerOpenGLWidget::waypointAdded,
            this, [this](const Waypoint &wp)
            {
                updateWaypointTable();
                onWaypointSelected(wp.sequence());
                emitWaypointsChanged(); });
}

void PathPlannerWidget::setupControls()
{
    // Waypoint group
    m_waypointGroup = new QGroupBox("Waypoints");
    m_waypointGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    m_controlsLayout->addWidget(m_waypointGroup);

    QVBoxLayout *waypointLayout = new QVBoxLayout(m_waypointGroup);

    // Waypoint count
    m_waypointCountLabel = new QLabel("Count: 0");
    waypointLayout->addWidget(m_waypointCountLabel);

    // Waypoint buttons
    QHBoxLayout *waypointButtonsLayout = new QHBoxLayout;
    m_addWaypointButton = new QPushButton("Add");
    m_removeWaypointButton = new QPushButton("Remove");

    waypointButtonsLayout->addWidget(m_addWaypointButton);
    waypointButtonsLayout->addWidget(m_removeWaypointButton);
    waypointLayout->addLayout(waypointButtonsLayout);

    // Path group
    m_pathGroup = new QGroupBox("Path");
    m_pathGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    m_controlsLayout->addWidget(m_pathGroup);

    QVBoxLayout *pathLayout = new QVBoxLayout(m_pathGroup);

    // Path name
    pathLayout->addWidget(new QLabel("Name:"));
    m_pathNameEdit = new QLineEdit("New Path");
    pathLayout->addWidget(m_pathNameEdit);

    // Path length
    m_pathLengthLabel = new QLabel("Length: 0.0 m");
    pathLayout->addWidget(m_pathLengthLabel);

    // Path buttons
    QGridLayout *pathButtonsLayout = new QGridLayout;
    m_clearPathButton = new QPushButton("Clear");
    m_savePathButton = new QPushButton("Save");
    m_loadPathButton = new QPushButton("Load");

    pathButtonsLayout->addWidget(m_clearPathButton, 0, 0);
    pathButtonsLayout->addWidget(m_savePathButton, 0, 1);
    pathButtonsLayout->addWidget(m_loadPathButton, 1, 0);

    m_playPathButton = new QPushButton("Play");
    m_stopPathButton = new QPushButton("Stop");
    pathButtonsLayout->addWidget(m_playPathButton, 1, 1);
    pathButtonsLayout->addWidget(m_stopPathButton, 2, 0);

    pathLayout->addLayout(pathButtonsLayout);
    
    // Mission control buttons
    pathLayout->addWidget(new QLabel("Mission Control:"));
    QGridLayout *missionButtonsLayout = new QGridLayout;
    
    m_uploadMissionButton = new QPushButton("Upload Mission");
    m_uploadMissionButton->setStyleSheet(
        "QPushButton { background-color: #10b981; color: white; border: none; padding: 6px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #059669; } "
        "QPushButton:disabled { background-color: #6b7280; }");
    m_uploadMissionButton->setEnabled(false);
    missionButtonsLayout->addWidget(m_uploadMissionButton, 0, 0, 1, 2);
    
    m_runMissionButton = new QPushButton("Run");
    m_runMissionButton->setEnabled(false);
    missionButtonsLayout->addWidget(m_runMissionButton, 1, 0);
    
    m_cancelMissionButton = new QPushButton("Cancel");
    m_cancelMissionButton->setEnabled(false);
    missionButtonsLayout->addWidget(m_cancelMissionButton, 1, 1);
    
    pathLayout->addLayout(missionButtonsLayout);
    
    // Mission status
    m_missionStatusLabel = new QLabel("Status: No mission uploaded");
    m_missionStatusLabel->setStyleSheet("color: #9ca3af; font-size: 11px;");
    m_missionStatusLabel->setWordWrap(true);
    pathLayout->addWidget(m_missionStatusLabel);

    // Path Order group (visible only when 2+ waypoints exist)
    m_pathOrderGroup = new QGroupBox("Path Order");
    m_pathOrderGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    m_controlsLayout->addWidget(m_pathOrderGroup);
    m_pathOrderGroup->setVisible(false);  // Hidden initially

    QVBoxLayout *pathOrderLayout = new QVBoxLayout(m_pathOrderGroup);

    m_sequentialOrderButton = new QPushButton("Sequential Order");
    m_sequentialOrderButton->setToolTip("Visit waypoints in the order they were placed");
    pathOrderLayout->addWidget(m_sequentialOrderButton);

    m_customOrderButton = new QPushButton("Custom Order...");
    m_customOrderButton->setToolTip("Choose a custom order to visit waypoints");
    pathOrderLayout->addWidget(m_customOrderButton);

    m_undoReorderButton = new QPushButton("Undo Reorder");
    m_undoReorderButton->setToolTip("Restore the previous waypoint order");
    m_undoReorderButton->setEnabled(false);
    pathOrderLayout->addWidget(m_undoReorderButton);

    // View group
    m_viewGroup = new QGroupBox("View");
    m_viewGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    m_controlsLayout->addWidget(m_viewGroup);

    QVBoxLayout *viewLayout = new QVBoxLayout(m_viewGroup);

    // View mode toggle
    m_viewModeButton = new QPushButton("Switch to Top-Down");
    m_viewModeButton->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; border: none; padding: 8px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #2563eb; }");
    viewLayout->addWidget(m_viewModeButton);

    m_resetCameraButton = new QPushButton("Reset Camera");
    viewLayout->addWidget(m_resetCameraButton);

    // Default altitude for top-down planning
    QHBoxLayout *altitudeLayout = new QHBoxLayout;
    altitudeLayout->addWidget(new QLabel("Default Altitude:"));
    m_defaultAltitudeSpinBox = new QDoubleSpinBox;
    m_defaultAltitudeSpinBox->setRange(0.0, 100.0);
    m_defaultAltitudeSpinBox->setValue(2.0);
    m_defaultAltitudeSpinBox->setSingleStep(0.5);
    m_defaultAltitudeSpinBox->setSuffix(" m");
    altitudeLayout->addWidget(m_defaultAltitudeSpinBox);
    viewLayout->addLayout(altitudeLayout);

    viewLayout->addWidget(new QLabel("Grid Size:"));
    m_gridSizeSlider = new QSlider(Qt::Horizontal);
    m_gridSizeSlider->setRange(5, 50);
    m_gridSizeSlider->setValue(20);
    viewLayout->addWidget(m_gridSizeSlider);

    viewLayout->addWidget(new QLabel("Coordinate System:"));
    m_coordinateSystemCombo = new QComboBox;
    m_coordinateSystemCombo->addItems({"NED", "ENU", "Aircraft"});
    viewLayout->addWidget(m_coordinateSystemCombo);

    m_controlsLayout->addStretch();

    // Connect signals
    connect(m_addWaypointButton, &QPushButton::clicked, this, &PathPlannerWidget::onAddWaypoint);
    connect(m_removeWaypointButton, &QPushButton::clicked, this, &PathPlannerWidget::onRemoveWaypoint);
    connect(m_clearPathButton, &QPushButton::clicked, this, &PathPlannerWidget::onClearPath);
    connect(m_savePathButton, &QPushButton::clicked, this, &PathPlannerWidget::onSavePath);
    connect(m_loadPathButton, &QPushButton::clicked, this, &PathPlannerWidget::onLoadPath);
    connect(m_uploadMissionButton, &QPushButton::clicked, this, &PathPlannerWidget::onUploadMission);
    connect(m_runMissionButton, &QPushButton::clicked, this, &PathPlannerWidget::onRunMission);
    connect(m_cancelMissionButton, &QPushButton::clicked, this, &PathPlannerWidget::onCancelMission);
    connect(m_playPathButton, &QPushButton::clicked, this, &PathPlannerWidget::onPlayPath);
    connect(m_stopPathButton, &QPushButton::clicked, this, &PathPlannerWidget::onStopPath);
    connect(m_resetCameraButton, &QPushButton::clicked, this, &PathPlannerWidget::onCameraReset);

    connect(m_gridSizeSlider, &QSlider::valueChanged, this, &PathPlannerWidget::onGridSizeChanged);
    connect(m_coordinateSystemCombo, &QComboBox::currentTextChanged, this, &PathPlannerWidget::onCoordinateSystemChanged);
    connect(m_viewModeButton, &QPushButton::clicked, this, &PathPlannerWidget::onViewModeChanged);
    connect(m_defaultAltitudeSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value)
            { m_openglWidget->setDefaultAltitude(static_cast<float>(value)); });

    // Path order connections
    connect(m_sequentialOrderButton, &QPushButton::clicked, this, &PathPlannerWidget::onSequentialOrder);
    connect(m_customOrderButton, &QPushButton::clicked, this, &PathPlannerWidget::onCustomOrder);
    connect(m_undoReorderButton, &QPushButton::clicked, this, &PathPlannerWidget::onUndoReorder);
}

void PathPlannerWidget::setupWaypointTable()
{
    m_waypointTable = new QTableWidget;
    m_waypointTable->setColumnCount(7);
    m_waypointTable->setHorizontalHeaderLabels({"Order", "X", "Y", "Z", "Yaw", "Speed", "Hold"});
    m_waypointTable->setMaximumHeight(200);
    m_waypointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_waypointTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_waypointTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_waypointTable->setStyleSheet(
        "QTableWidget { background-color: #1f2937; color: white; border: 1px solid #4b5563; gridline-color: #374151; } "
        "QTableWidget::item { padding: 4px; } "
        "QTableWidget::item:selected { background-color: #3b82f6; } "
        "QHeaderView::section { background-color: #374151; color: white; padding: 4px; border: 1px solid #4b5563; }");

    // Set column widths
    m_waypointTable->setColumnWidth(0, 40); // ID
    m_waypointTable->setColumnWidth(1, 60); // X
    m_waypointTable->setColumnWidth(2, 60); // Y
    m_waypointTable->setColumnWidth(3, 60); // Z
    m_waypointTable->setColumnWidth(4, 60); // Yaw
    m_waypointTable->setColumnWidth(5, 60); // Speed
    m_waypointTable->setColumnWidth(6, 60); // Hold

    // Insert waypoint table after waypoint count label
    QVBoxLayout *waypointLayout = qobject_cast<QVBoxLayout *>(m_waypointGroup->layout());
    waypointLayout->insertWidget(1, m_waypointTable);

    connect(m_waypointTable, &QTableWidget::cellChanged, this, &PathPlannerWidget::onWaypointCellChanged);
    connect(m_waypointTable, &QTableWidget::currentCellChanged,
            this, [this](int currentRow, int, int, int)
            {
                if (currentRow >= 0 && currentRow < m_waypointTable->rowCount()) {
                    QTableWidgetItem *idItem = m_waypointTable->item(currentRow, 0);
                    if (idItem) {
                        onWaypointSelected(idItem->text().toInt());
                    }
                } });
}

void PathPlannerWidget::onAddWaypoint()
{
    float defaultAlt = m_defaultAltitudeSpinBox->value();
    QVector3D newPoint(0, defaultAlt, 0);
    m_openglWidget->addWaypoint(newPoint);
    // updateWaypointTable and signal emission handled by waypointAdded signal
}

void PathPlannerWidget::onRemoveWaypoint()
{
    if (m_selectedWaypoint >= 0)
    {
        m_openglWidget->removeWaypoint(m_selectedWaypoint);
        updateWaypointTable();
        m_selectedWaypoint = -1;
        emitWaypointsChanged();
    }
}

void PathPlannerWidget::onClearPath()
{
    if (!m_openglWidget)
        return;

    m_openglWidget->clearWaypoints();
    m_selectedWaypoint = -1;

    if (m_waypointTable)
        updateWaypointTable();

    emitWaypointsChanged();
}

void PathPlannerWidget::onSavePath()
{
    // Check if there are any waypoints to save
    if (m_openglWidget->waypoints().empty())
    {
        QMessageBox::warning(this, "Save Path", "No waypoints to save. Please add at least one waypoint.");
        return;
    }

    // Get path name from the path name edit field or prompt for one
    QString pathName = m_pathNameEdit->text().trimmed();
    if (pathName.isEmpty() || pathName == "New Path")
    {
        bool ok;
        pathName = QInputDialog::getText(this, "Save Path",
                                         "Enter path name:",
                                         QLineEdit::Normal,
                                         "Flight Path",
                                         &ok);
        if (!ok || pathName.trimmed().isEmpty())
        {
            return; // User cancelled
        }
        pathName = pathName.trimmed();
    }

    // Get the paths folder - use SOURCE_DIR defined by CMake at compile time
    QString appDir = QCoreApplication::applicationDirPath();
    QString pathsDir;
    QDir dir;
    
    // First priority: Use the source directory paths folder (for development)
#ifdef SOURCE_DIR
    pathsDir = QString(SOURCE_DIR) + "/paths";
    dir.setPath(pathsDir);
    if (dir.exists()) {
        // Use source paths directory
    } else {
        // Create it if it doesn't exist
        dir.mkpath(pathsDir);
    }
#else
    // Fallback: paths folder next to the executable (for distribution)
    pathsDir = appDir + "/paths";
    dir.setPath(pathsDir);
    if (!dir.exists()) {
        dir.mkpath(pathsDir);
    }
#endif

    // Generate a filename from the path name (sanitize for filesystem)
    QString sanitizedName = pathName;
    sanitizedName.replace(QRegularExpression("[^a-zA-Z0-9_\\-\\s]"), "");
    sanitizedName.replace(" ", "_");
    if (sanitizedName.isEmpty())
    {
        sanitizedName = "path";
    }

    // Add timestamp to make filename unique
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = dir.absoluteFilePath(sanitizedName + "_" + timestamp + ".json");

    if (saveToJson(fileName))
    {
        // Update the path name edit field
        m_pathNameEdit->setText(pathName);

        // Convert waypoints to QVector<QVector3D> for the signal
        QVector<QVector3D> points;
        for (const auto &wp : m_openglWidget->waypoints())
        {
            points.append(QVector3D(wp.x(), wp.y(), wp.z()));
        }

        // Emit signal so Flight History can update
        emit pathSaved(pathName, points);

        QMessageBox::information(this, "Save Path", 
                                QString("Path '%1' saved successfully to:\n%2").arg(pathName).arg(fileName));
    }
    else
    {
        QMessageBox::warning(this, "Save Path", "Failed to save path.");
    }
}

void PathPlannerWidget::onLoadPath()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "Load Path",
                                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                    "JSON Files (*.json)");

    if (!fileName.isEmpty())
    {
        if (loadFromJson(fileName))
        {
            QMessageBox::information(this, "Load Path", "Path loaded successfully!");
        }
        else
        {
            QMessageBox::warning(this, "Load Path", "Failed to load path.");
        }
    }
}

void PathPlannerWidget::onWaypointSelected(int id)
{
    m_selectedWaypoint = id;
    m_openglWidget->setSelectedWaypoint(id);

    // Select the corresponding row in the table
    for (int row = 0; row < m_waypointTable->rowCount(); ++row)
    {
        QTableWidgetItem *idItem = m_waypointTable->item(row, 0);
        if (idItem && idItem->data(Qt::UserRole).toInt() == id)
        {
            m_waypointTable->selectRow(row);
            break;
        }
    }
}

void PathPlannerWidget::onWaypointCellChanged(int row, int column)
{
    // Ignore Order column changes
    if (column == 0 || row < 0 || row >= m_waypointTable->rowCount())
        return;

    QTableWidgetItem *idItem = m_waypointTable->item(row, 0);
    if (!idItem)
        return;

    int id = idItem->data(Qt::UserRole).toInt();

    // Find the waypoint and update it
    const auto &waypoints = m_openglWidget->waypoints();
    for (const auto &wp : waypoints)
    {
        if (wp.sequence() == id)
        {
            Waypoint updated = wp;

            // Update the changed field
            QTableWidgetItem *item = m_waypointTable->item(row, column);
            if (!item)
                return;

            bool ok;
            float value = item->text().toFloat(&ok);
            if (!ok)
                return;

            switch (column)
            {
            case 1:
                updated.setX(value);
                break;
            case 2:
                updated.setY(value);
                break;
            case 3:
                updated.setZ(value);
                break;
            case 4:
                updated.setYawAngle(value);
                break;
            case 5:
                // Speed is not in the new Waypoint model - skip
                break;
            case 6:
                updated.setHoldTime(value);
                break;
            }

            m_openglWidget->updateWaypoint(id, updated);
            emitWaypointsChanged();
            break;
        }
    }
}

void PathPlannerWidget::onCameraReset()
{
    m_openglWidget->resetCamera();
}

void PathPlannerWidget::onViewModeChanged()
{
    if (m_openglWidget->viewMode() == PathPlannerOpenGLWidget::TopDownMode)
    {
        m_openglWidget->setViewMode(PathPlannerOpenGLWidget::View3DMode);
        m_viewModeButton->setText("Switch to Top-Down");
    }
    else
    {
        m_openglWidget->setViewMode(PathPlannerOpenGLWidget::TopDownMode);
        m_viewModeButton->setText("Switch to 3D View");
    }
}

void PathPlannerWidget::onPlayPath()
{
    if (!m_isPlayingPath && !m_openglWidget->waypoints().empty())
    {
        startPathAnimation();
    }
}

void PathPlannerWidget::onStopPath()
{
    if (m_isPlayingPath)
    {
        stopPathAnimation();
    }
}

void PathPlannerWidget::onPathAnimationTimer()
{
    // Simple path animation logic
    m_animationProgress += 0.02f;
    if (m_animationProgress >= 1.0f)
    {
        m_animationProgress = 0.0f;
        m_currentAnimationWaypoint++;

        const auto &waypoints = m_openglWidget->waypoints();
        if (m_currentAnimationWaypoint >= static_cast<int>(waypoints.size()))
        {
            stopPathAnimation();
            return;
        }
    }

    // Update visualization (this would show a moving drone along the path)
    m_openglWidget->update();
}

void PathPlannerWidget::onGridSizeChanged(int size)
{
    // Update grid size in OpenGL widget
    m_openglWidget->update();
}

void PathPlannerWidget::onCoordinateSystemChanged(const QString &system)
{
    // Update coordinate system display
    m_openglWidget->update();
}

void PathPlannerWidget::updateWaypointTable()
{
    if (!m_waypointTable || !m_openglWidget)
        return;

    // Block signals to prevent triggering cellChanged during update
    m_waypointTable->blockSignals(true);
    m_waypointTable->clearContents();
    m_waypointTable->setRowCount(0);

    const auto &waypoints = m_openglWidget->waypoints();

    if (!waypoints.empty())
    {
        m_waypointTable->setRowCount(waypoints.size());

        for (size_t i = 0; i < waypoints.size(); ++i)
        {
            const Waypoint &wp = waypoints[i];

            // Label as letter (read-only) - shows visit order number and waypoint letter
            QString label = QString("%1. %2").arg(i + 1).arg(idToLetter(wp.sequence()));
            QTableWidgetItem *idItem = new QTableWidgetItem(label);
            idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
            idItem->setData(Qt::UserRole, wp.sequence());  // Store actual sequence for reference
            m_waypointTable->setItem(i, 0, idItem);

            // Position and parameters (editable)
            m_waypointTable->setItem(i, 1, new QTableWidgetItem(QString::number(wp.x(), 'f', 2)));
            m_waypointTable->setItem(i, 2, new QTableWidgetItem(QString::number(wp.y(), 'f', 2)));
            m_waypointTable->setItem(i, 3, new QTableWidgetItem(QString::number(wp.z(), 'f', 2)));
            m_waypointTable->setItem(i, 4, new QTableWidgetItem(QString::number(wp.yawAngle(), 'f', 1)));
            m_waypointTable->setItem(i, 5, new QTableWidgetItem("0.0")); // Speed not in new model
            m_waypointTable->setItem(i, 6, new QTableWidgetItem(QString::number(wp.holdTime(), 'f', 1)));
        }
    }

    m_waypointTable->blockSignals(false);

    if (m_waypointCountLabel)
        m_waypointCountLabel->setText(QString("Count: %1").arg(waypoints.size()));

    // Calculate path length
    float totalLength = 0.0f;
    if (waypoints.size() > 1)
    {
        for (size_t i = 0; i < waypoints.size() - 1; ++i)
        {
            QVector3D p1(waypoints[i].x(), waypoints[i].y(), waypoints[i].z());
            QVector3D p2(waypoints[i + 1].x(), waypoints[i + 1].y(), waypoints[i + 1].z());
            totalLength += p1.distanceToPoint(p2);
        }
    }

    if (m_pathLengthLabel)
        m_pathLengthLabel->setText(QString("Length: %1 m").arg(totalLength, 0, 'f', 1));

    if (m_removeWaypointButton)
        m_removeWaypointButton->setEnabled(m_selectedWaypoint >= 0);

    updatePathOrderVisibility();
}

void PathPlannerWidget::startPathAnimation()
{
    m_isPlayingPath = true;
    m_currentAnimationWaypoint = 0;
    m_animationProgress = 0.0f;
    m_pathAnimationTimer->start();

    m_playPathButton->setEnabled(false);
    m_stopPathButton->setEnabled(true);
}

void PathPlannerWidget::stopPathAnimation()
{
    m_isPlayingPath = false;
    m_pathAnimationTimer->stop();

    m_playPathButton->setEnabled(true);
    m_stopPathButton->setEnabled(false);
}

void PathPlannerWidget::loadPoints(const QVector<QVector3D> &points)
{
    // Convert legacy QVector<QVector3D> to std::vector<Waypoint>
    std::vector<Waypoint> waypoints;
    for (int i = 0; i < points.size(); ++i)
    {
        Waypoint wp(points[i]);
        wp.setSequence(i + 1);
        waypoints.push_back(wp);
    }
    m_openglWidget->setWaypoints(waypoints);
    updateWaypointTable();
    if (!waypoints.empty())
    {
        onWaypointSelected(1);
    }
}

void PathPlannerWidget::clearPath()
{
    onClearPath();
}

// New public API methods

void PathPlannerWidget::addWaypoint(const QVector3D &pos)
{
    m_openglWidget->addWaypoint(pos);
}

void PathPlannerWidget::updateWaypoint(int id, const Waypoint &wp)
{
    m_openglWidget->updateWaypoint(id, wp);
    updateWaypointTable();
    emitWaypointsChanged();
}

void PathPlannerWidget::removeWaypoint(int id)
{
    m_openglWidget->removeWaypoint(id);
    updateWaypointTable();
    emitWaypointsChanged();
}

const std::vector<Waypoint> &PathPlannerWidget::waypoints() const
{
    return m_openglWidget->waypoints();
}

void PathPlannerWidget::emitWaypointsChanged()
{
    emit waypointsChanged(m_openglWidget->waypoints());
}

bool PathPlannerWidget::saveToJson(const QString &path)
{
    QJsonObject root;
    root["version"] = 1;

    QJsonArray waypointsArray;
    const auto &waypoints = m_openglWidget->waypoints();
    for (const auto &wp : waypoints)
    {
        waypointsArray.append(wp.toJson());
    }
    root["waypoints"] = waypointsArray;

    QJsonDocument doc(root);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool PathPlannerWidget::loadFromJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject())
    {
        return false;
    }

    QJsonObject root = doc.object();
    int version = root["version"].toInt();

    if (version != 1)
    {
        return false; // Unsupported version
    }

    QJsonArray waypointsArray = root["waypoints"].toArray();
    std::vector<Waypoint> waypoints;

    for (const QJsonValue &value : waypointsArray)
    {
        if (value.isObject())
        {
            waypoints.push_back(Waypoint::fromJson(value.toObject()));
        }
    }

    m_openglWidget->setWaypoints(waypoints);
    updateWaypointTable();
    emitWaypointsChanged();

    if (!waypoints.empty())
    {
        onWaypointSelected(waypoints[0].sequence());
    }

    return true;
}

void PathPlannerWidget::updatePathOrderVisibility()
{
    if (m_pathOrderGroup)
    {
        bool hasEnoughWaypoints = m_openglWidget->waypoints().size() >= 2;
        m_pathOrderGroup->setVisible(hasEnoughWaypoints);
    }
}

void PathPlannerWidget::onSequentialOrder()
{
    const auto &waypoints = m_openglWidget->waypoints();
    if (waypoints.size() < 2)
        return;

    // Save current order for undo
    m_previousWaypointOrder = waypoints;
    m_undoReorderButton->setEnabled(true);

    // Sort waypoints by their original ID (placement order: A, B, C, ...)
    // IDs stay the same - only the array position changes
    std::vector<Waypoint> sorted = waypoints;
    std::sort(sorted.begin(), sorted.end(), [](const Waypoint &a, const Waypoint &b) {
        return a.sequence() < b.sequence();
    });

    m_openglWidget->setWaypoints(sorted);
    updateWaypointTable();
    emitWaypointsChanged();
}

void PathPlannerWidget::onCustomOrder()
{
    const auto &waypoints = m_openglWidget->waypoints();
    if (waypoints.size() < 2)
        return;

    // Save current order for undo
    m_previousWaypointOrder = waypoints;

    // Create dialog for custom ordering
    QDialog dialog(this);
    dialog.setWindowTitle("Custom Path Order");
    dialog.setMinimumWidth(350);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *instructions = new QLabel("Drag items to set the visit order.\nWaypoint labels (A, B, C...) stay fixed.\nThe drone will visit waypoints in this order:");
    layout->addWidget(instructions);

    QListWidget *listWidget = new QListWidget;
    listWidget->setDragDropMode(QAbstractItemView::InternalMove);
    for (const auto &wp : waypoints)
    {
        QString text = QString("Waypoint %1 (%.1f, %.1f, %.1f)")
                           .arg(idToLetter(wp.sequence()))
                           .arg(wp.x())
                           .arg(wp.y())
                           .arg(wp.z());
        QListWidgetItem *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, wp.sequence());
        listWidget->addItem(item);
    }
    layout->addWidget(listWidget);

    // Up/Down buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    QPushButton *upButton = new QPushButton("Move Up");
    QPushButton *downButton = new QPushButton("Move Down");
    buttonLayout->addWidget(upButton);
    buttonLayout->addWidget(downButton);
    layout->addLayout(buttonLayout);

    connect(upButton, &QPushButton::clicked, [listWidget]() {
        int row = listWidget->currentRow();
        if (row > 0)
        {
            QListWidgetItem *item = listWidget->takeItem(row);
            listWidget->insertItem(row - 1, item);
            listWidget->setCurrentRow(row - 1);
        }
    });

    connect(downButton, &QPushButton::clicked, [listWidget]() {
        int row = listWidget->currentRow();
        if (row >= 0 && row < listWidget->count() - 1)
        {
            QListWidgetItem *item = listWidget->takeItem(row);
            listWidget->insertItem(row + 1, item);
            listWidget->setCurrentRow(row + 1);
        }
    });

    // OK/Cancel buttons
    QHBoxLayout *dialogButtons = new QHBoxLayout;
    QPushButton *okButton = new QPushButton("Apply");
    QPushButton *cancelButton = new QPushButton("Cancel");
    dialogButtons->addStretch();
    dialogButtons->addWidget(okButton);
    dialogButtons->addWidget(cancelButton);
    layout->addLayout(dialogButtons);

    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
    {
        // Build new waypoint order based on list widget order
        // Sequence numbers stay the same - only the array position changes
        std::vector<Waypoint> newOrder;
        for (int i = 0; i < listWidget->count(); ++i)
        {
            int originalSequence = listWidget->item(i)->data(Qt::UserRole).toInt();
            // Find the waypoint with this sequence
            for (const auto &wp : waypoints)
            {
                if (wp.sequence() == originalSequence)
                {
                    // Keep the waypoint exactly as is - sequence doesn't change
                    newOrder.push_back(wp);
                    break;
                }
            }
        }

        m_openglWidget->setWaypoints(newOrder);
        updateWaypointTable();
        emitWaypointsChanged();
        m_undoReorderButton->setEnabled(true);
    }
}

void PathPlannerWidget::onUndoReorder()
{
    if (m_previousWaypointOrder.empty())
        return;

    m_openglWidget->setWaypoints(m_previousWaypointOrder);
    updateWaypointTable();
    emitWaypointsChanged();

    m_previousWaypointOrder.clear();
    m_undoReorderButton->setEnabled(false);
}

void PathPlannerWidget::setDroneController(DroneController *controller)
{
    m_droneController = controller;
    
    if (m_droneController) {
        // Enable upload button when waypoints exist
        connect(this, &PathPlannerWidget::waypointsChanged,
                this, [this](const std::vector<Waypoint> &waypoints) {
                    m_uploadMissionButton->setEnabled(!waypoints.empty() && m_droneController != nullptr);
                });
    }
}

void PathPlannerWidget::onUploadMission()
{
    if (!m_droneController) {
        QMessageBox::warning(this, "Upload Failed", "Drone controller not initialized.");
        return;
    }
    
    const auto &waypoints = m_openglWidget->waypoints();
    if (waypoints.empty()) {
        QMessageBox::warning(this, "Upload Failed", "No waypoints to upload.");
        return;
    }
    
    // Create FlightPath from current waypoints
    FlightPath missionPath;
    missionPath.setName(m_pathNameEdit->text());
    missionPath.setHomeRelative(true);
    missionPath.setTakeoffAltM(m_defaultAltitudeSpinBox->value());
    missionPath.setCruiseSpeedMS(5.0f);  // Default cruise speed
    missionPath.setAutoLand(true);
    
    for (const Waypoint &wp : waypoints) {
        missionPath.addWaypoint(wp);
    }
    
    // Save mission to temporary JSON file
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString missionFileName = QString("mission_%1.json").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString missionFilePath = QDir(tempDir).filePath(missionFileName);
    
    QFile file(missionFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Upload Failed", "Failed to create mission file.");
        return;
    }
    
    file.write(QJsonDocument(missionPath.toJson()).toJson(QJsonDocument::Indented));
    file.close();
    
    // Upload via VOXLConnection
    m_missionStatusLabel->setText("Status: Uploading mission...");
    m_missionStatusLabel->setStyleSheet("color: #fbbf24;");  // Yellow
    m_uploadMissionButton->setEnabled(false);
    
    // Convert waypoints to legacy format for DroneController
    QVector<QVector3D> legacyWaypoints;
    for (const Waypoint &wp : waypoints) {
        legacyWaypoints.append(QVector3D(wp.x(), wp.y(), wp.z()));
    }
    
    m_droneController->uploadMission(legacyWaypoints);
    
    // Show success message
    QMessageBox::information(this, "Mission Upload", 
        QString("Mission uploaded successfully!\n\nWaypoints: %1\nTotal Distance: %2 m")
        .arg(waypoints.size())
        .arg(missionPath.totalDistance(), 0, 'f', 2));
    
    m_missionStatusLabel->setText("Status: Mission uploaded");
    m_missionStatusLabel->setStyleSheet("color: #10b981;");  // Green
    m_uploadMissionButton->setEnabled(true);
    m_runMissionButton->setEnabled(true);
    
    // Clean up temp file
    QFile::remove(missionFilePath);
}

void PathPlannerWidget::onRunMission()
{
    if (!m_droneController) {
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Run Mission", 
        "Are you sure you want to start the mission?\n\nThe drone will take off and follow the uploaded waypoints.",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        m_droneController->startMission();
        m_missionStatusLabel->setText("Status: Mission running...");
        m_missionStatusLabel->setStyleSheet("color: #3b82f6;");  // Blue
        m_runMissionButton->setEnabled(false);
        m_cancelMissionButton->setEnabled(true);
    }
}

void PathPlannerWidget::onCancelMission()
{
    if (!m_droneController) {
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Cancel Mission", 
        "Are you sure you want to cancel the mission?\n\nThe drone will stop following the mission and hover.",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        m_droneController->abortMission();
        m_missionStatusLabel->setText("Status: Mission cancelled");
        m_missionStatusLabel->setStyleSheet("color: #ef4444;");  // Red
        m_runMissionButton->setEnabled(true);
        m_cancelMissionButton->setEnabled(false);
    }
}

void PathPlannerWidget::onMissionUploadComplete(bool success, const QString &message)
{
    if (success) {
        m_missionStatusLabel->setText("Status: " + message);
        m_missionStatusLabel->setStyleSheet("color: #10b981;");  // Green
        m_runMissionButton->setEnabled(true);
    } else {
        m_missionStatusLabel->setText("Status: Upload failed - " + message);
        m_missionStatusLabel->setStyleSheet("color: #ef4444;");  // Red
    }
    m_uploadMissionButton->setEnabled(true);
}

void PathPlannerWidget::onMissionStatusReceived(const QString &status)
{
    m_missionStatusLabel->setText("Status: " + status);
}