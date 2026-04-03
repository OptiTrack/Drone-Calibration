#include "pathplannerwidget.h"
#include "../controllers/dronecontroller.h"
#include "../models/flightpath.h"
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QtMath>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>
#include <QScrollArea>
#include <QShortcut>
#include <QHeaderView>
#include <QAbstractItemModel>
#include <QVector2D>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <algorithm>
#include <cmath>

static QString plannerPathsDirectory()
{
#ifdef SOURCE_DIR
    QString sourcePathsDir = QString(SOURCE_DIR) + "/paths";
    QDir sourceDir(sourcePathsDir);
    if (!sourceDir.exists())
        QDir().mkpath(sourcePathsDir);
    return sourceDir.absolutePath();
#else
    const QString exePathsDir = QCoreApplication::applicationDirPath() + "/paths";
    QDir exeDir(exePathsDir);
    if (!exeDir.exists())
        QDir().mkpath(exePathsDir);
    return exeDir.absolutePath();
#endif
}

// Logical waypoint coordinates use:
//   X = forward axis (maps to world +Z),
//   Y = right axis   (maps to world +X),
//   Z = altitude     (maps to world +Y, vertical).
static QVector3D logicalToWorld(const QVector3D &logicalPos)
{
    return QVector3D(logicalPos.y(), logicalPos.z(), logicalPos.x());
}

static QVector3D worldToLogical(const QVector3D &worldPos)
{
    return QVector3D(worldPos.z(), worldPos.x(), worldPos.y());
}

static QVector3D waypointToWorld(const Waypoint &wp)
{
    return logicalToWorld(QVector3D(wp.x(), wp.y(), wp.z()));
}

// Heading in world XZ from logical yaw (degrees). 0° = world +Z (logical +X / forward).
static QVector3D logicalYawHeadingWorld(float yawDeg)
{
    const float rad = qDegreesToRadians(yawDeg);
    return QVector3D(std::sin(rad), 0.0f, std::cos(rad));
}

static float wrapYawDegrees180(float deg)
{
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0.0f)
        deg += 360.0f;
    return deg - 180.0f;
}

// Waypoint body (sphere + cone ring) — ~50% larger than original 0.11 m sphere.
static constexpr float kWaypointSphereRadiusWorld = 0.165f;
// Keep same cone length / sphere radius ratio as before the size bump.
static constexpr float kWaypointConeBeyondM = 0.36f * (kWaypointSphereRadiusWorld / 0.11f);

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
    : QOpenGLWidget(parent), m_shaderProgram(nullptr), m_cameraPosition(0, 5, 10), m_cameraTarget(0, 0, 0), m_cameraUp(0, 1, 0), m_cameraDistance(15.0f), m_cameraYaw(0.0f), m_cameraPitch(30.0f), m_viewMode(View3DMode), m_interactionMode(TransformMode), m_orthoZoom(10.0f), m_defaultAltitude(2.0f), m_selectedWaypoint(-1), m_hoveredWaypoint(-1), m_hoveredSegment(-1), m_mousePressed(false), m_isDragging(false), m_draggingTransform(false), m_activeHandle(TransformHandle::None), m_hasHoverPreview(false), m_applyingHistory(false), m_dragGizmoOriginWorld(), m_dragYawPlaneAngleStartRad(0.0f), m_dragStartYawDeg(0.0f), m_animationTime(0.0f)
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
    drawGizmo();

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

QPoint PathPlannerOpenGLWidget::worldToScreen(const QVector3D &worldPos) const
{
    QPoint px;
    if (!projectWorldToScreenDepth(worldPos, &px, nullptr))
        return QPoint(-1000, -1000);
    return px;
}

bool PathPlannerOpenGLWidget::projectWorldToScreenDepth(const QVector3D &worldPos, QPoint *outPx, float *outNdcZ) const
{
    const QVector4D clipPos = m_projectionMatrix * m_viewMatrix * QVector4D(worldPos, 1.0f);
    if (qAbs(clipPos.w()) < 0.0001f)
        return false;
    const QVector3D ndcPos = clipPos.toVector3D() / clipPos.w();
    if (outPx)
    {
        outPx->setX(static_cast<int>((ndcPos.x() + 1.0f) * 0.5f * width()));
        outPx->setY(static_cast<int>((1.0f - ndcPos.y()) * 0.5f * height()));
    }
    if (outNdcZ)
        *outNdcZ = ndcPos.z();
    return true;
}

float PathPlannerOpenGLWidget::projectedSphereRadiusPxRaw(const QVector3D &worldCenter, float worldRadius) const
{
    if (worldRadius <= 0.0f)
        return 12.0f;
    QVector3D viewDir = m_cameraTarget - m_cameraPosition;
    if (viewDir.lengthSquared() < 1e-8f)
        return 12.0f;
    viewDir.normalize();
    QVector3D right = QVector3D::crossProduct(viewDir, m_cameraUp);
    if (right.lengthSquared() < 1e-8f)
        right = QVector3D::crossProduct(viewDir, QVector3D(1.0f, 0.0f, 0.0f));
    if (right.lengthSquared() < 1e-8f)
        return 12.0f;
    right.normalize();
    const QVector3D up = QVector3D::crossProduct(right, viewDir).normalized();

    QPoint pr, pnr, pu, pdu;
    if (!projectWorldToScreenDepth(worldCenter + right * worldRadius, &pr, nullptr) ||
        !projectWorldToScreenDepth(worldCenter - right * worldRadius, &pnr, nullptr))
        return 12.0f;
    const float halfW = static_cast<float>(QLineF(pr, pnr).length()) * 0.5f;
    float halfH = halfW;
    if (projectWorldToScreenDepth(worldCenter + up * worldRadius, &pu, nullptr) &&
        projectWorldToScreenDepth(worldCenter - up * worldRadius, &pdu, nullptr))
    {
        halfH = static_cast<float>(QLineF(pu, pdu).length()) * 0.5f;
    }
    return 0.5f * (halfW + halfH);
}

float PathPlannerOpenGLWidget::screenSpaceSphereRadiusPx(const QVector3D &worldCenter, float worldRadius) const
{
    const float r = projectedSphereRadiusPxRaw(worldCenter, worldRadius);
    if (r <= 0.0f)
        return 12.0f;
    const float maxR = qMax(100.0f, 0.42f * static_cast<float>(qMin(width(), height())));
    return qBound(8.0f, r, maxR);
}

void PathPlannerOpenGLWidget::waypointMarkerConeParameters(const QVector3D &waypointCenterWorld, float &outLengthScale,
                                                           float &outBaseRadiusScale, float &outRingAlongF) const
{
    const float raw = projectedSphereRadiusPxRaw(waypointCenterWorld, kWaypointSphereRadiusWorld);

    float zoomT = 0.0f;
    if (m_viewMode == TopDownMode)
        zoomT = qBound(0.0f, (m_orthoZoom - 5.0f) / (42.0f - 5.0f), 1.0f);
    else
        zoomT = qBound(0.0f, (m_cameraDistance - 6.0f) / (48.0f - 6.0f), 1.0f);

    // Zoomed out: longer, slightly thicker; zoomed in: a bit shorter. Independent of HUD min-size clamp.
    outLengthScale = 0.84f + zoomT * 0.38f;

    // When the true projection is tiny, the HUD floor-kicks in — bias ring toward sphere center (“mid”) and widen slightly.
    const float tinyT = qBound(0.0f, (10.0f - raw) / 10.0f, 1.0f);
    // Wider cone at zoom-out: keep the ring “seam” on the sphere (we apply thickness to the tip),
    // but make the body noticeably thicker when zoomed out.
    outBaseRadiusScale = 1.0f + zoomT * 0.55f + tinyT * 0.30f;

    const float depthT = qBound(0.0f, raw / 16.0f, 1.0f);
    // Slide the ring toward the sphere center at small projections (the “mid” you described).
    outRingAlongF = 0.32f + depthT * 0.54f;
}

/// Yaw handle distance along heading: halfway between red axis (logical X) tip and purple plane-XZ handle, in top-down (XZ) distance.
/// Red tip offset (0,0,L) → ground radius L. Purple XZ offset (0, 0.45L, 0.45L) → ground radius 0.45L.
static float gizmoYawTipRadialDistance(float axisLength)
{
    const float axisXTipGround = axisLength;
    const float planeXzGround = 0.45f * axisLength;
    return 0.5f * (axisXTipGround + planeXzGround);
}

void PathPlannerOpenGLWidget::drawWaypointLabels(QPainter &painter)
{
    // Set up font for labels
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(12);
    painter.setFont(font);

    if (m_hasHoverPreview && m_interactionMode == CreateMode)
    {
        const QPoint ghostPos = worldToScreen(m_lastHoverPreviewWorldPos);
        if (ghostPos.x() >= 0 && ghostPos.x() <= width() && ghostPos.y() >= 0 && ghostPos.y() <= height())
        {
            painter.setPen(QPen(QColor(180, 220, 255, 220), 2, Qt::DashLine));
            painter.setBrush(QColor(60, 120, 180, 60));
            painter.drawEllipse(ghostPos, 10, 10);
        }
    }

    if (m_waypoints.empty())
        return;

    font.setBold(false);
    font.setPointSize(9);
    painter.setFont(font);
    painter.setPen(QColor(255, 220, 160));
    for (size_t i = 0; i + 1 < m_waypoints.size(); ++i)
    {
        const QVector3D p1 = waypointToWorld(m_waypoints[i]);
        const QVector3D p2 = waypointToWorld(m_waypoints[i + 1]);
        const QPoint mid = worldToScreen((p1 + p2) * 0.5f);
        const float segLen = p1.distanceToPoint(p2);
        painter.drawText(mid + QPoint(6, -4), QString::number(segLen, 'f', 1) + "m");
    }

    struct WaypointHudEntry
    {
        QPoint screenCenter;
        float zCenterNdc;
        QColor fillColor;
        QString indexText;
        float badgeRadiusPx = 15.0f;
    };
    QVector<WaypointHudEntry> hudEntries;
    hudEntries.reserve(static_cast<int>(m_waypoints.size()));

    const float kHudSphereR = kWaypointSphereRadiusWorld;

    for (size_t i = 0; i < m_waypoints.size(); ++i)
    {
        const Waypoint &wp = m_waypoints[i];
        const QVector3D worldPos = waypointToWorld(wp);
        QPoint screenPos;
        float zC = 0.0f;
        if (!projectWorldToScreenDepth(worldPos, &screenPos, &zC))
            continue;
        if (screenPos.x() < -50 || screenPos.x() > width() + 50 ||
            screenPos.y() < -50 || screenPos.y() > height() + 50)
            continue;

        QVector3D rgb;
        if (wp.sequence() == m_selectedWaypoint)
            rgb = QVector3D(0.56f, 0.76f, 1.0f);
        else if (m_selectedWaypointIds.contains(wp.sequence()))
            rgb = QVector3D(0.2f, 0.72f, 0.95f);
        else if (wp.sequence() == m_hoveredWaypoint)
            rgb = QVector3D(0.38f, 0.65f, 1.0f);
        else
            rgb = QVector3D(0.12f, 0.42f, 0.92f);

        WaypointHudEntry e;
        e.screenCenter = screenPos;
        e.zCenterNdc = zC;
        e.fillColor = QColor::fromRgbF(rgb.x(), rgb.y(), rgb.z());
        e.indexText = QString::number(wp.sequence());
        e.badgeRadiusPx = screenSpaceSphereRadiusPx(worldPos, kHudSphereR);
        hudEntries.append(e);
    }

    // Pass 1: back-to-front discs (match 3D occlusion)
    std::sort(hudEntries.begin(), hudEntries.end(),
              [](const WaypointHudEntry &a, const WaypointHudEntry &b) { return a.zCenterNdc > b.zCenterNdc; });
    for (const WaypointHudEntry &e : hudEntries)
    {
        const float outlineMax = qMin(14.0f, qMax(5.0f, e.badgeRadiusPx * 0.065f));
        const int outlineW = qMax(2, static_cast<int>(qBound(1.5f, e.badgeRadiusPx * 0.11f, outlineMax)));
        painter.setBrush(e.fillColor);
        painter.setPen(QPen(Qt::white, outlineW));
        painter.drawEllipse(e.screenCenter, static_cast<int>(e.badgeRadiusPx + 0.5f), static_cast<int>(e.badgeRadiusPx + 0.5f));
    }

    // Pass 2: front-to-back index so nearer labels read on top
    std::sort(hudEntries.begin(), hudEntries.end(),
              [](const WaypointHudEntry &a, const WaypointHudEntry &b) { return a.zCenterNdc < b.zCenterNdc; });
    font.setBold(true);
    for (const WaypointHudEntry &e : hudEntries)
    {
        QFont idxFont = font;
        const float digitScale = e.indexText.size() > 1 ? 0.88f : 1.0f;
        const int px = qBound(10, static_cast<int>(e.badgeRadiusPx * 1.12f * digitScale + 0.5f), 512);
        idxFont.setPixelSize(px);
        painter.setFont(idxFont);

        QPainterPath textPath;
        textPath.addText(0, 0, idxFont, e.indexText);
        const QRectF br = textPath.boundingRect();
        textPath.translate(-br.center());

        painter.translate(e.screenCenter);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawPath(textPath);
        painter.translate(-e.screenCenter);
    }

    if (m_interactionMode == TransformMode && m_selectedWaypoint >= 0)
    {
        const float yawDeg = selectedWaypointYawDeg();
        const float yawTipR = gizmoYawTipRadialDistance(1.3f);
        const QVector3D tip = gizmoCenterWorld() + logicalYawHeadingWorld(yawDeg) * yawTipR;
        const QPoint tipPx = worldToScreen(tip);
        if (tipPx.x() >= -80 && tipPx.x() <= width() + 80 && tipPx.y() >= -80 && tipPx.y() <= height() + 80)
        {
            font.setBold(true);
            font.setPointSize(10);
            painter.setFont(font);
            painter.setPen(QColor(255, 210, 120));
            painter.drawText(tipPx + QPoint(10, 4),
                             QString::number(yawDeg, 'f', 0) + QChar(0x00B0));
        }
    }

    float totalLength = 0.0f;
    if (m_waypoints.size() > 1)
    {
        for (size_t i = 0; i + 1 < m_waypoints.size(); ++i)
        {
            const QVector3D p1 = waypointToWorld(m_waypoints[i]);
            const QVector3D p2 = waypointToWorld(m_waypoints[i + 1]);
            totalLength += p1.distanceToPoint(p2);
        }
    }

    painter.setPen(QColor(215, 223, 235));
    font.setBold(true);
    font.setPointSize(10);
    painter.setFont(font);
    const QString overlayText = QString("Count: %1   Length: %2 m")
                                    .arg(static_cast<int>(m_waypoints.size()))
                                    .arg(totalLength, 0, 'f', 1);
    const QFontMetrics fm(font);
    const int pad = 10;
    painter.drawText(width() - fm.horizontalAdvance(overlayText) - pad,
                     height() - fm.height() + fm.ascent() - pad,
                     overlayText);
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
    
    float axisLength = 1.0f;
    float axisRadius = 0.05f;
    
    // Logical X axis (red): world +Z direction
    generateCylinderVertices(QVector3D(0, 0, 0), QVector3D(0, 0, axisLength), axisRadius,
                             QVector3D(1.0f, 0.0f, 0.0f), axesVertices, axesColors);
    
    // Logical Y axis (green): world +X direction
    generateCylinderVertices(QVector3D(0, 0, 0), QVector3D(axisLength, 0, 0), axisRadius,
                             QVector3D(0.0f, 1.0f, 0.0f), axesVertices, axesColors);
    
    // Logical Z axis (blue): world +Y direction (vertical)
    generateCylinderVertices(QVector3D(0, 0, 0), QVector3D(0, axisLength, 0), axisRadius,
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

// Truncated cone between two circular sections (same axis), blunt nose + high segment count reads smoother than a needle cone.
static void generateFrustumConeMesh(const QVector3D &baseCenter, float baseRadius, const QVector3D &tipCenter,
                                    float tipRadius, int segments, const QVector3D &color, QVector<float> &vertices,
                                    QVector<float> &colors)
{
    const QVector3D axis = tipCenter - baseCenter;
    const float h = axis.length();
    if (h < 1e-5f || baseRadius < 1e-5f)
        return;
    const QVector3D n = axis / h;
    QVector3D perp1 = QVector3D::crossProduct(n, QVector3D(0.0f, 1.0f, 0.0f));
    if (perp1.length() < 1e-4f)
        perp1 = QVector3D::crossProduct(n, QVector3D(1.0f, 0.0f, 0.0f));
    perp1.normalize();
    const QVector3D perp2 = QVector3D::crossProduct(n, perp1).normalized();

    const float tipR = qMax(tipRadius, 1e-4f);

    for (int i = 0; i < segments; ++i)
    {
        const float a1 = (2.0f * static_cast<float>(M_PI) * static_cast<float>(i)) / static_cast<float>(segments);
        const float a2 = (2.0f * static_cast<float>(M_PI) * static_cast<float>(i + 1)) / static_cast<float>(segments);
        const QVector3D b1 = baseCenter + (perp1 * std::cos(a1) + perp2 * std::sin(a1)) * baseRadius;
        const QVector3D b2 = baseCenter + (perp1 * std::cos(a2) + perp2 * std::sin(a2)) * baseRadius;
        const QVector3D t1 = tipCenter + (perp1 * std::cos(a1) + perp2 * std::sin(a1)) * tipR;
        const QVector3D t2 = tipCenter + (perp1 * std::cos(a2) + perp2 * std::sin(a2)) * tipR;
        vertices << b1.x() << b1.y() << b1.z();
        vertices << t1.x() << t1.y() << t1.z();
        vertices << b2.x() << b2.y() << b2.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
        vertices << b2.x() << b2.y() << b2.z();
        vertices << t1.x() << t1.y() << t1.z();
        vertices << t2.x() << t2.y() << t2.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
    }

    const QVector3D tipPole = tipCenter + n * qMax(0.004f * h, 0.25f * tipR);
    for (int i = 0; i < segments; ++i)
    {
        const float a1 = (2.0f * static_cast<float>(M_PI) * static_cast<float>(i)) / static_cast<float>(segments);
        const float a2 = (2.0f * static_cast<float>(M_PI) * static_cast<float>(i + 1)) / static_cast<float>(segments);
        const QVector3D t1 = tipCenter + (perp1 * std::cos(a1) + perp2 * std::sin(a1)) * tipR;
        const QVector3D t2 = tipCenter + (perp1 * std::cos(a2) + perp2 * std::sin(a2)) * tipR;
        vertices << tipPole.x() << tipPole.y() << tipPole.z();
        vertices << t1.x() << t1.y() << t1.z();
        vertices << t2.x() << t2.y() << t2.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
        colors << color.x() << color.y() << color.z();
    }
}

// Blue sphere + forward cone. Ring slides toward sphere center when zoomed out (matches HUD “mid” read); length/thickness follow camera.
static void generateOrientedWaypointMarker(const QVector3D &centerWorld, float yawDeg, const QVector3D &sphereColor,
                                           float coneLengthScale, float ringAlongF, float baseRadiusScale,
                                           QVector<float> &vertices, QVector<float> &colors)
{
    const QVector3D forward = logicalYawHeadingWorld(yawDeg);
    const float R = kWaypointSphereRadiusWorld;
    const float rf = qBound(0.12f, ringAlongF, 0.92f);
    const float d = rf * R;
    const float rRingSq = R * R - d * d;
    if (rRingSq <= 1e-8f)
        return;
    // Keep the ring exactly on the sphere for consistent centering/seam across zoom.
    const float rRing = std::sqrt(rRingSq);
    const float coneBeyond = kWaypointConeBeyondM * coneLengthScale;
    const QVector3D ringPlaneCenter = centerWorld + forward * d;
    const float coneLen = (R + coneBeyond) - d;
    if (coneLen < 1e-4f)
        return;
    const QVector3D coneColor(0.93f, 0.95f, 1.0f);
    // Lower tip fraction => blunter/rounder cone head.
    constexpr float kTipFrac = 0.76f;
    const float tipOffset = coneLen * kTipFrac;
    const float tipRLinear = rRing * (1.0f - kTipFrac);
    // Apply "thickness" to the tip disk; the base ring stays fixed on the sphere.
    const float tipR = qMax(tipRLinear * baseRadiusScale, 0.12f * rRing);
    const QVector3D tipDiskCenter = ringPlaneCenter + forward * tipOffset;

    generateSphereVertices(centerWorld, R, sphereColor, vertices, colors);
    constexpr int kConeSegs = 32;
    generateFrustumConeMesh(ringPlaneCenter, rRing, tipDiskCenter, tipR, kConeSegs, coneColor, vertices, colors);
}

void PathPlannerOpenGLWidget::drawWaypoints()
{
    if (m_waypoints.empty())
        return;

    QVector<float> waypointVertices;
    QVector<float> waypointColors;

    for (size_t i = 0; i < m_waypoints.size(); ++i)
    {
        const Waypoint &wp = m_waypoints[i];
        const QVector3D center = waypointToWorld(wp);
        QVector3D color;

        if (wp.sequence() == m_selectedWaypoint)
        {
            color = QVector3D(0.56f, 0.76f, 1.0f);
        }
        else if (m_selectedWaypointIds.contains(wp.sequence()))
        {
            color = QVector3D(0.2f, 0.72f, 0.95f);
        }
        else if (wp.sequence() == m_hoveredWaypoint)
        {
            color = QVector3D(0.38f, 0.65f, 1.0f);
        }
        else
        {
            color = QVector3D(0.12f, 0.42f, 0.92f);
        }

        float lenS = 1.0f, baseS = 1.0f, ringF = 0.86f;
        waypointMarkerConeParameters(center, lenS, baseS, ringF);
        generateOrientedWaypointMarker(center, wp.yawAngle(), color, lenS, ringF, baseS, waypointVertices, waypointColors);
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

void PathPlannerOpenGLWidget::appendLine(const QVector3D &a, const QVector3D &b, const QVector3D &color,
                                         QVector<float> &vertices, QVector<float> &colors) const
{
    vertices << a.x() << a.y() << a.z();
    vertices << b.x() << b.y() << b.z();
    colors << color.x() << color.y() << color.z();
    colors << color.x() << color.y() << color.z();
}

void PathPlannerOpenGLWidget::appendSphere(const QVector3D &center, float radius, const QVector3D &color,
                                           QVector<float> &vertices, QVector<float> &colors) const
{
    generateSphereVertices(center, radius, color, vertices, colors);
}

QVector3D PathPlannerOpenGLWidget::gizmoCenterWorld() const
{
    for (const Waypoint &wp : m_waypoints)
    {
        if (wp.sequence() == m_selectedWaypoint)
        {
            return waypointToWorld(wp);
        }
    }
    return QVector3D();
}

QVector3D PathPlannerOpenGLWidget::gizmoAxisDirectionWorld(TransformHandle handle) const
{
    switch (handle)
    {
    case TransformHandle::AxisX:
        return QVector3D(0.0f, 0.0f, 1.0f);
    case TransformHandle::AxisY:
        return QVector3D(1.0f, 0.0f, 0.0f);
    case TransformHandle::AxisZ:
        return QVector3D(0.0f, 1.0f, 0.0f);
    default:
        return QVector3D();
    }
}

float PathPlannerOpenGLWidget::worldDeltaAlongAxisFromScreenDelta(const QVector3D &originWorld,
                                                                  const QVector3D &axisWorldUnit,
                                                                  const QPoint &screenDelta) const
{
    const float eps = 0.5f;
    const QPoint p0 = worldToScreen(originWorld);
    const QPoint p1 = worldToScreen(originWorld + axisWorldUnit * eps);
    const QVector2D s(static_cast<float>(p1.x() - p0.x()), static_cast<float>(p1.y() - p0.y()));
    const float lenSq = QVector2D::dotProduct(s, s);
    if (lenSq < 1e-6f)
        return 0.0f;
    const QVector2D d(static_cast<float>(screenDelta.x()), static_cast<float>(screenDelta.y()));
    return eps * QVector2D::dotProduct(d, s) / lenSq;
}

bool PathPlannerOpenGLWidget::worldDeltasOnPlaneFromScreenDelta(const QVector3D &originWorld,
                                                                const QVector3D &u0World,
                                                                const QVector3D &u1World,
                                                                const QPoint &screenDelta,
                                                                float &outDu0,
                                                                float &outDu1) const
{
    const float eps = 0.5f;
    const QPoint originPx = worldToScreen(originWorld);
    const auto deltaPx = [this, &originWorld, &originPx, eps](const QVector3D &u) {
        const QPoint p = worldToScreen(originWorld + u * eps);
        return QVector2D(static_cast<float>(p.x() - originPx.x()), static_cast<float>(p.y() - originPx.y()));
    };
    const QVector2D s0 = deltaPx(u0World);
    const QVector2D s1 = deltaPx(u1World);
    const float det = s0.x() * s1.y() - s0.y() * s1.x();
    if (qAbs(det) < 1e-4f)
        return false;
    const QVector2D m(static_cast<float>(screenDelta.x()), static_cast<float>(screenDelta.y()));
    outDu0 = eps * (m.x() * s1.y() - m.y() * s1.x()) / det;
    outDu1 = eps * (s0.x() * m.y() - s0.y() * m.x()) / det;
    return true;
}

// Gizmo handles (world space at waypoint): logical X -> world +Z (red), logical Y -> world +X (green),
// logical Z -> world +Y (blue, altitude). Orange = logical XY, purple = XZ, cyan = YZ. Gold arrow + ring = yaw (heading in horizontal plane, degrees).
void PathPlannerOpenGLWidget::drawGizmo()
{
    if (m_selectedWaypoint < 0 || m_interactionMode != TransformMode)
        return;

    const QVector3D center = gizmoCenterWorld();
    const float axisLength = 1.3f;
    const float handleRadius = 0.11f;
    const float yawDeg = selectedWaypointYawDeg();
    const float yawRad = qDegreesToRadians(yawDeg);
    const QVector3D yawFwd = logicalYawHeadingWorld(yawDeg);
    const float yawArrowLen = gizmoYawTipRadialDistance(axisLength);
    const float yawArcRadius = 0.42f * axisLength;

    QVector<float> lineVertices;
    QVector<float> lineColors;
    QVector<float> handleVertices;
    QVector<float> handleColors;

    const QVector3D xEnd = center + QVector3D(0.0f, 0.0f, axisLength);
    const QVector3D yEnd = center + QVector3D(axisLength, 0.0f, 0.0f);
    const QVector3D zEnd = center + QVector3D(0.0f, axisLength, 0.0f);

    auto axisColor = [this](TransformHandle handle, const QVector3D &defaultColor) {
        return (m_activeHandle == handle) ? QVector3D(1.0f, 1.0f, 0.2f) : defaultColor;
    };

    appendLine(center, xEnd, axisColor(TransformHandle::AxisX, QVector3D(1.0f, 0.2f, 0.2f)), lineVertices, lineColors);
    appendLine(center, yEnd, axisColor(TransformHandle::AxisY, QVector3D(0.2f, 1.0f, 0.2f)), lineVertices, lineColors);
    appendLine(center, zEnd, axisColor(TransformHandle::AxisZ, QVector3D(0.2f, 0.4f, 1.0f)), lineVertices, lineColors);

    appendSphere(xEnd, handleRadius, axisColor(TransformHandle::AxisX, QVector3D(1.0f, 0.2f, 0.2f)), handleVertices, handleColors);
    appendSphere(yEnd, handleRadius, axisColor(TransformHandle::AxisY, QVector3D(0.2f, 1.0f, 0.2f)), handleVertices, handleColors);
    appendSphere(zEnd, handleRadius, axisColor(TransformHandle::AxisZ, QVector3D(0.2f, 0.4f, 1.0f)), handleVertices, handleColors);

    const QVector3D xyCenter = center + QVector3D(0.45f * axisLength, 0.0f, 0.45f * axisLength);
    const QVector3D xzCenter = center + QVector3D(0.0f, 0.45f * axisLength, 0.45f * axisLength);
    const QVector3D yzCenter = center + QVector3D(0.45f * axisLength, 0.45f * axisLength, 0.0f);
    appendSphere(xyCenter, handleRadius * 0.8f, axisColor(TransformHandle::PlaneXY, QVector3D(1.0f, 0.6f, 0.2f)), handleVertices, handleColors);
    appendSphere(xzCenter, handleRadius * 0.8f, axisColor(TransformHandle::PlaneXZ, QVector3D(0.8f, 0.3f, 1.0f)), handleVertices, handleColors);
    appendSphere(yzCenter, handleRadius * 0.8f, axisColor(TransformHandle::PlaneYZ, QVector3D(0.2f, 1.0f, 1.0f)), handleVertices, handleColors);

    // Yaw: reference tick at 0° (logical forward = world +Z), sweep arc, heading arrow, draggable cap at tip.
    {
        const QVector3D refTickEnd = center + QVector3D(0.0f, 0.0f, 0.35f * axisLength);
        appendLine(center, refTickEnd, QVector3D(0.35f, 0.38f, 0.45f), lineVertices, lineColors);
        const int arcSegs = qBound(4, static_cast<int>(std::ceil(std::fabs(yawDeg) / 12.0f)), 36);
        if (std::fabs(yawDeg) > 0.5f)
        {
            for (int i = 0; i < arcSegs; ++i)
            {
                const float t0 = (static_cast<float>(i) / static_cast<float>(arcSegs)) * yawRad;
                const float t1 = (static_cast<float>(i + 1) / static_cast<float>(arcSegs)) * yawRad;
                const QVector3D p0(center.x() + std::sin(t0) * yawArcRadius, center.y(), center.z() + std::cos(t0) * yawArcRadius);
                const QVector3D p1(center.x() + std::sin(t1) * yawArcRadius, center.y(), center.z() + std::cos(t1) * yawArcRadius);
                appendLine(p0, p1, QVector3D(0.75f, 0.55f, 0.12f), lineVertices, lineColors);
            }
        }
        const QVector3D yawTip = center + yawFwd * yawArrowLen;
        appendLine(center, yawTip, axisColor(TransformHandle::Yaw, QVector3D(0.95f, 0.72f, 0.08f)), lineVertices, lineColors);
        appendSphere(yawTip, handleRadius * 0.85f, axisColor(TransformHandle::Yaw, QVector3D(0.98f, 0.78f, 0.1f)), handleVertices, handleColors);
    }

    if (!lineVertices.isEmpty())
    {
        m_vao.bind();
        m_vertexBuffer.bind();
        m_vertexBuffer.allocate(lineVertices.data(), lineVertices.size() * sizeof(float));
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        QOpenGLBuffer colorBuffer;
        colorBuffer.create();
        colorBuffer.bind();
        colorBuffer.allocate(lineColors.data(), lineColors.size() * sizeof(float));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);

        glLineWidth(2.5f);
        glDrawArrays(GL_LINES, 0, lineVertices.size() / 3);
        glLineWidth(1.0f);

        colorBuffer.release();
        m_vertexBuffer.release();
        m_vao.release();
    }

    if (!handleVertices.isEmpty())
    {
        m_vao.bind();
        m_vertexBuffer.bind();
        m_vertexBuffer.allocate(handleVertices.data(), handleVertices.size() * sizeof(float));
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        QOpenGLBuffer colorBuffer;
        colorBuffer.create();
        colorBuffer.bind();
        colorBuffer.allocate(handleColors.data(), handleColors.size() * sizeof(float));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLES, 0, handleVertices.size() / 3);

        colorBuffer.release();
        m_vertexBuffer.release();
        m_vao.release();
    }
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
        const QVector3D p1 = waypointToWorld(wp1);
        const QVector3D p2 = waypointToWorld(wp2);

        pathVertices << p1.x() << p1.y() << p1.z();
        pathVertices << p2.x() << p2.y() << p2.z();

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
    updateCamera();
    m_lastMousePos = event->pos();
    m_mousePressed = true;
    m_dragStartMousePos = event->pos();

    if (event->button() == Qt::LeftButton)
    {
        const Qt::KeyboardModifiers mods = event->modifiers();

        if (m_interactionMode == TransformMode && m_selectedWaypoint >= 0)
        {
            const TransformHandle handle = findTransformHandleAt(event->pos());
            if (handle != TransformHandle::None)
            {
                m_draggingTransform = true;
                m_activeHandle = handle;
                m_dragStartLogicalPos = selectedWaypointLogicalPosition();
                m_dragGizmoOriginWorld = gizmoCenterWorld();
                m_dragBeforeSnapshot = m_waypoints;
                if (handle == TransformHandle::Yaw)
                {
                    const QVector3D c = m_dragGizmoOriginWorld;
                    const QVector3D hit = screenToWorldOnYPlane(event->pos(), c.y());
                    m_dragYawPlaneAngleStartRad = std::atan2(hit.x() - c.x(), hit.z() - c.z());
                    m_dragStartYawDeg = selectedWaypointYawDeg();
                }
                emit transformStarted(m_selectedWaypoint);
                return;
            }
        }

        float pickDistancePx = 0.0f;
        float pickDepthNdc = 0.0f;
        const int waypointId = findWaypointAt(event->pos(), &pickDistancePx, &pickDepthNdc);

        if (waypointId >= 0)
        {
            if ((mods & Qt::ControlModifier) && m_interactionMode == TransformMode)
            {
                if (m_selectedWaypointIds.contains(waypointId))
                {
                    m_selectedWaypointIds.remove(waypointId);
                    if (m_selectedWaypoint == waypointId)
                    {
                        m_selectedWaypoint = m_selectedWaypointIds.isEmpty() ? -1 : *m_selectedWaypointIds.begin();
                    }
                }
                else
                {
                    m_selectedWaypointIds.insert(waypointId);
                    m_selectedWaypoint = waypointId;
                }
            }
            else
            {
                m_selectedWaypointIds.clear();
                m_selectedWaypointIds.insert(waypointId);
                m_selectedWaypoint = waypointId;
            }

            emit waypointSelected(waypointId);
            emit selectionChanged(m_selectedWaypointIds);
            update();
            return;
        }

        if (m_interactionMode == CreateMode && (mods & Qt::ControlModifier) && m_waypoints.size() > 1)
        {
            QVector3D insertWorld;
            const int segmentIndex = findSegmentAt(event->pos(), &insertWorld, 14.0f);
            if (segmentIndex >= 0)
            {
                const std::vector<Waypoint> before = m_waypoints;
                Waypoint wp(worldToLogical(insertWorld));
                m_waypoints.insert(m_waypoints.begin() + segmentIndex + 1, wp);
                for (int i = 0; i < static_cast<int>(m_waypoints.size()); ++i)
                {
                    m_waypoints[i].setSequence(i + 1);
                }
                m_selectedWaypoint = segmentIndex + 2;
                m_selectedWaypointIds.clear();
                m_selectedWaypointIds.insert(m_selectedWaypoint);
                commitEdit(before, m_waypoints);
                emit waypointSelected(m_selectedWaypoint);
                emit waypointsEdited();
                update();
                return;
            }
        }

        const bool shouldCreate = (m_interactionMode == CreateMode);
        if (shouldCreate)
        {
            QVector3D worldPos;
            if (m_viewMode == TopDownMode)
            {
                worldPos = screenToWorld(event->pos(), 0.0f);
                worldPos.setY(m_defaultAltitude);
            }
            else
            {
                worldPos = screenToWorldOnYPlane(event->pos(), m_defaultAltitude);
            }
            addWaypoint(worldToLogical(worldPos));
            return;
        }

        if (m_interactionMode == SelectMode || m_interactionMode == TransformMode)
        {
            m_selectedWaypointIds.clear();
            m_selectedWaypoint = -1;
            emit selectionChanged(m_selectedWaypointIds);
            update();
        }
    }
}

void PathPlannerOpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    updateCamera();

    float hoverDistance = 0.0f;
    float hoverDepth = 0.0f;
    m_hoveredWaypoint = findWaypointAt(event->pos(), &hoverDistance, &hoverDepth);
    if (m_interactionMode == CreateMode)
    {
        if (m_viewMode == TopDownMode)
        {
            m_lastHoverPreviewWorldPos = screenToWorld(event->pos(), 0.0f);
            m_lastHoverPreviewWorldPos.setY(m_defaultAltitude);
        }
        else
        {
            m_lastHoverPreviewWorldPos = screenToWorldOnYPlane(event->pos(), m_defaultAltitude);
        }
        m_hasHoverPreview = true;
    }
    else
    {
        m_hasHoverPreview = false;
    }

    QPoint delta = event->pos() - m_lastMousePos;

    if ((event->buttons() & Qt::LeftButton) && m_draggingTransform && m_selectedWaypoint >= 0)
    {
        const QPoint totalDelta = event->pos() - m_dragStartMousePos;
        const QVector3D &origin = m_dragGizmoOriginWorld;

        if (m_activeHandle == TransformHandle::Yaw)
        {
            const QVector3D hit = screenToWorldOnYPlane(event->pos(), origin.y());
            const float ang = std::atan2(hit.x() - origin.x(), hit.z() - origin.z());
            float deltaRad = ang - m_dragYawPlaneAngleStartRad;
            constexpr float kPi = 3.14159265f;
            while (deltaRad > kPi)
                deltaRad -= 2.0f * kPi;
            while (deltaRad < -kPi)
                deltaRad += 2.0f * kPi;
            float newYaw = wrapYawDegrees180(m_dragStartYawDeg + qRadiansToDegrees(deltaRad));
            if (event->modifiers() & Qt::ShiftModifier)
                newYaw = wrapYawDegrees180(qRound(newYaw / 15.0f) * 15.0f);

            if (updateWaypointYawAngle(m_selectedWaypoint, newYaw))
            {
                QVector3D worldPos;
                for (const Waypoint &w : m_waypoints)
                {
                    if (w.sequence() == m_selectedWaypoint)
                    {
                        worldPos = waypointToWorld(w);
                        break;
                    }
                }
                emit waypointMoved(m_selectedWaypoint, worldPos);
                emit transformUpdated(m_selectedWaypoint);
                emit waypointsEdited();
            }
            m_lastMousePos = event->pos();
            return;
        }

        QVector3D pos = m_dragStartLogicalPos;

        switch (m_activeHandle)
        {
        case TransformHandle::AxisX:
            pos.setX(pos.x() + worldDeltaAlongAxisFromScreenDelta(origin, QVector3D(0.0f, 0.0f, 1.0f), totalDelta));
            break;
        case TransformHandle::AxisY:
            pos.setY(pos.y() + worldDeltaAlongAxisFromScreenDelta(origin, QVector3D(1.0f, 0.0f, 0.0f), totalDelta));
            break;
        case TransformHandle::AxisZ:
            pos.setZ(pos.z() + worldDeltaAlongAxisFromScreenDelta(origin, QVector3D(0.0f, 1.0f, 0.0f), totalDelta));
            break;
        case TransformHandle::PlaneXY: {
            float dForward = 0.0f;
            float dRight = 0.0f;
            if (worldDeltasOnPlaneFromScreenDelta(origin, QVector3D(0.0f, 0.0f, 1.0f), QVector3D(1.0f, 0.0f, 0.0f),
                                                 totalDelta, dForward, dRight))
            {
                pos.setX(pos.x() + dForward);
                pos.setY(pos.y() + dRight);
            }
            break;
        }
        case TransformHandle::PlaneXZ: {
            float dForward = 0.0f;
            float dUp = 0.0f;
            if (worldDeltasOnPlaneFromScreenDelta(origin, QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, 1.0f, 0.0f),
                                                 totalDelta, dForward, dUp))
            {
                pos.setX(pos.x() + dForward);
                pos.setZ(pos.z() + dUp);
            }
            break;
        }
        case TransformHandle::PlaneYZ: {
            float dRight = 0.0f;
            float dUp = 0.0f;
            if (worldDeltasOnPlaneFromScreenDelta(origin, QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f),
                                                 totalDelta, dRight, dUp))
            {
                pos.setY(pos.y() + dRight);
                pos.setZ(pos.z() + dUp);
            }
            break;
        }
        case TransformHandle::Yaw:
        case TransformHandle::None:
            break;
        }

        if (event->modifiers() & Qt::ShiftModifier)
        {
            const float snap = 0.25f;
            pos.setX(qRound(pos.x() / snap) * snap);
            pos.setY(qRound(pos.y() / snap) * snap);
            pos.setZ(qRound(pos.z() / snap) * snap);
        }

        if (updateWaypointLogicalPosition(m_selectedWaypoint, pos))
        {
            QVector3D worldPos;
            for (const Waypoint &w : m_waypoints)
            {
                if (w.sequence() == m_selectedWaypoint)
                {
                    worldPos = waypointToWorld(w);
                    break;
                }
            }
            emit waypointMoved(m_selectedWaypoint, worldPos);
            emit transformUpdated(m_selectedWaypoint);
            emit waypointsEdited();
        }
        m_lastMousePos = event->pos();
        return;
    }

    // Only allow camera manipulation in 3D mode
    if (m_mousePressed && m_viewMode == View3DMode)
    {
        if (event->buttons() & Qt::RightButton)
        {
            // Camera rotation
            m_cameraYaw += delta.x() * 0.5f;
            m_cameraPitch += delta.y() * 0.5f;
            m_cameraPitch = qBound(-89.0f, m_cameraPitch, 89.0f);
            update();
        }
        else if (event->buttons() & Qt::MiddleButton)
        {
            // Camera panning
            float sensitivity = 0.01f;
            QVector3D right = QVector3D::crossProduct(m_cameraTarget - m_cameraPosition, m_cameraUp).normalized();
            QVector3D up = QVector3D::crossProduct(right, m_cameraTarget - m_cameraPosition).normalized();
            m_cameraTarget -= right * delta.x() * sensitivity;
            m_cameraTarget += up * delta.y() * sensitivity;
            update();
        }
    }
    else if (m_mousePressed && m_viewMode == TopDownMode)
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

    m_lastMousePos = event->pos();
    update();
}

void PathPlannerOpenGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (m_draggingTransform)
    {
        m_draggingTransform = false;
        m_activeHandle = TransformHandle::None;
        commitEdit(m_dragBeforeSnapshot, m_waypoints);
        emit transformEnded(m_selectedWaypoint);
        emit waypointsEdited();
    }
    m_mousePressed = false;
    update();
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
    return screenToWorldOnYPlane(screenPos, depth);
}

QVector3D PathPlannerOpenGLWidget::screenToWorldOnYPlane(const QPoint &screenPos, float worldY) const
{
    const float ndcX = (2.0f * static_cast<float>(screenPos.x())) / qMax(1, width()) - 1.0f;
    const float ndcY = 1.0f - (2.0f * static_cast<float>(screenPos.y())) / qMax(1, height());

    QVector4D nearClip(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farClip(ndcX, ndcY, 1.0f, 1.0f);
    const QMatrix4x4 invVP = (m_projectionMatrix * m_viewMatrix).inverted();

    QVector4D nearWorld4 = invVP * nearClip;
    QVector4D farWorld4 = invVP * farClip;
    if (qAbs(nearWorld4.w()) > 0.0001f)
        nearWorld4 /= nearWorld4.w();
    if (qAbs(farWorld4.w()) > 0.0001f)
        farWorld4 /= farWorld4.w();

    const QVector3D nearWorld = nearWorld4.toVector3D();
    const QVector3D farWorld = farWorld4.toVector3D();
    QVector3D dir = (farWorld - nearWorld);
    if (dir.lengthSquared() < 1e-8f)
        return nearWorld;
    dir.normalize();

    if (qAbs(dir.y()) < 1e-5f)
    {
        return QVector3D(nearWorld.x(), worldY, nearWorld.z());
    }

    float t = (worldY - nearWorld.y()) / dir.y();
    if (t < 0.0f)
        t = 0.0f;
    return nearWorld + dir * t;
}

int PathPlannerOpenGLWidget::findWaypointAt(const QPoint &screenPos, float *outDistancePx, float *outDepthNdc) const
{
    float bestDistance = 1e9f;
    float bestDepth = 1e9f;
    int bestId = -1;
    for (size_t i = 0; i < m_waypoints.size(); ++i)
    {
        const Waypoint &wp = m_waypoints[i];
        QVector3D wpPos = waypointToWorld(wp);
        const float pickRadiusPx = qBound(22.0f, screenSpaceSphereRadiusPx(wpPos, kWaypointSphereRadiusWorld) * 1.3f, 72.0f);

        QVector4D clipPos = m_projectionMatrix * m_viewMatrix * QVector4D(wpPos, 1.0f);
        if (qAbs(clipPos.w()) > 0.0001f)
        {
            QVector3D ndcPos = clipPos.toVector3D() / clipPos.w();
            QPoint screenPoint(
                static_cast<int>((ndcPos.x() + 1.0f) * 0.5f * width()),
                static_cast<int>((1.0f - ndcPos.y()) * 0.5f * height()));

            const float dx = static_cast<float>(screenPoint.x() - screenPos.x());
            const float dy = static_cast<float>(screenPoint.y() - screenPos.y());
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= pickRadiusPx && (dist < bestDistance || (qFuzzyCompare(dist, bestDistance) && ndcPos.z() < bestDepth)))
            {
                bestDistance = dist;
                bestDepth = ndcPos.z();
                bestId = wp.sequence();
            }
        }
    }

    if (outDistancePx)
        *outDistancePx = bestDistance;
    if (outDepthNdc)
        *outDepthNdc = bestDepth;
    return bestId;
}

int PathPlannerOpenGLWidget::findWaypointAt(const QPoint &screenPos)
{
    return findWaypointAt(screenPos, nullptr, nullptr);
}

int PathPlannerOpenGLWidget::findSegmentAt(const QPoint &screenPos, QVector3D *insertWorldPoint, float maxDistancePx) const
{
    if (m_waypoints.size() < 2)
        return -1;

    int bestSegment = -1;
    float bestDist = maxDistancePx;
    QVector3D bestWorld;

    for (size_t i = 0; i + 1 < m_waypoints.size(); ++i)
    {
        const QVector3D p1 = waypointToWorld(m_waypoints[i]);
        const QVector3D p2 = waypointToWorld(m_waypoints[i + 1]);
        const QPoint s1 = worldToScreen(p1);
        const QPoint s2 = worldToScreen(p2);

        const QVector2D a(s1.x(), s1.y());
        const QVector2D b(s2.x(), s2.y());
        const QVector2D p(screenPos.x(), screenPos.y());
        const QVector2D ab = b - a;
        const float abLenSq = QVector2D::dotProduct(ab, ab);
        if (abLenSq < 1e-4f)
            continue;
        float t = QVector2D::dotProduct(p - a, ab) / abLenSq;
        t = qBound(0.0f, t, 1.0f);
        const QVector2D proj = a + ab * t;
        const float dist = (p - proj).length();
        if (dist < bestDist)
        {
            bestDist = dist;
            bestSegment = static_cast<int>(i);
            bestWorld = p1 + (p2 - p1) * t;
        }
    }

    if (insertWorldPoint && bestSegment >= 0)
        *insertWorldPoint = bestWorld;
    return bestSegment;
}

PathPlannerOpenGLWidget::TransformHandle PathPlannerOpenGLWidget::findTransformHandleAt(const QPoint &screenPos) const
{
    if (m_selectedWaypoint < 0)
        return TransformHandle::None;

    const QVector3D center = gizmoCenterWorld();
    const float axisLength = 1.3f;
    const float yawTipR = gizmoYawTipRadialDistance(axisLength);
    const QVector3D yawTip = center + logicalYawHeadingWorld(selectedWaypointYawDeg()) * yawTipR;
    struct HandlePoint
    {
        TransformHandle handle;
        QPoint pos;
    };
    QVector<HandlePoint> points = {
        {TransformHandle::AxisX, worldToScreen(center + QVector3D(0.0f, 0.0f, axisLength))},
        {TransformHandle::AxisY, worldToScreen(center + QVector3D(axisLength, 0.0f, 0.0f))},
        {TransformHandle::AxisZ, worldToScreen(center + QVector3D(0.0f, axisLength, 0.0f))},
        {TransformHandle::PlaneXY, worldToScreen(center + QVector3D(0.45f * axisLength, 0.0f, 0.45f * axisLength))},
        {TransformHandle::PlaneXZ, worldToScreen(center + QVector3D(0.0f, 0.45f * axisLength, 0.45f * axisLength))},
        {TransformHandle::PlaneYZ, worldToScreen(center + QVector3D(0.45f * axisLength, 0.45f * axisLength, 0.0f))},
        {TransformHandle::Yaw, worldToScreen(yawTip)}
    };

    TransformHandle best = TransformHandle::None;
    float bestDist = 14.0f;
    for (const HandlePoint &hp : points)
    {
        const float dx = static_cast<float>(hp.pos.x() - screenPos.x());
        const float dy = static_cast<float>(hp.pos.y() - screenPos.y());
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < bestDist)
        {
            bestDist = d;
            best = hp.handle;
        }
    }
    return best;
}

QVector3D PathPlannerOpenGLWidget::selectedWaypointLogicalPosition() const
{
    for (const Waypoint &wp : m_waypoints)
    {
        if (wp.sequence() == m_selectedWaypoint)
            return QVector3D(wp.x(), wp.y(), wp.z());
    }
    return QVector3D();
}

float PathPlannerOpenGLWidget::selectedWaypointYawDeg() const
{
    for (const Waypoint &wp : m_waypoints)
    {
        if (wp.sequence() == m_selectedWaypoint)
            return wp.yawAngle();
    }
    return 0.0f;
}

bool PathPlannerOpenGLWidget::updateWaypointYawAngle(int id, float yawDeg)
{
    yawDeg = wrapYawDegrees180(yawDeg);
    for (Waypoint &waypoint : m_waypoints)
    {
        if (waypoint.sequence() == id)
        {
            if (qAbs(waypoint.yawAngle() - yawDeg) < 0.02f)
                return false;
            waypoint.setYawAngle(yawDeg);
            update();
            return true;
        }
    }
    return false;
}

bool PathPlannerOpenGLWidget::updateWaypointLogicalPosition(int id, const QVector3D &newLogicalPosition)
{
    constexpr float kMinAltitudeM = 0.3f;
    constexpr float kMaxAltitudeM = 100.0f;
    for (Waypoint &waypoint : m_waypoints)
    {
        if (waypoint.sequence() == id)
        {
            QVector3D clamped = newLogicalPosition;
            clamped.setZ(qBound(kMinAltitudeM, clamped.z(), kMaxAltitudeM));
            if ((QVector3D(waypoint.x(), waypoint.y(), waypoint.z()) - clamped).lengthSquared() < 1e-8f)
                return false;
            waypoint.setPosition(clamped);
            update();
            return true;
        }
    }
    return false;
}

void PathPlannerOpenGLWidget::setWaypoints(const std::vector<Waypoint> &waypoints)
{
    m_waypoints = waypoints;
    if (m_selectedWaypoint > static_cast<int>(m_waypoints.size()))
        m_selectedWaypoint = -1;
    update();
}

void PathPlannerOpenGLWidget::addWaypoint(const QVector3D &point)
{
    const std::vector<Waypoint> before = m_waypoints;

    // Generate new sequence number (1-based, sequential)
    int newSequence = 1;
    if (!m_waypoints.empty())
    {
        newSequence = m_waypoints.back().sequence() + 1;
    }

    Waypoint wp(point);
    wp.setSequence(newSequence);
    m_waypoints.push_back(wp);
    m_selectedWaypoint = newSequence;
    m_selectedWaypointIds.clear();
    m_selectedWaypointIds.insert(newSequence);
    commitEdit(before, m_waypoints);
    emit waypointAdded(wp);
    emit waypointsEdited();
    update();
}

void PathPlannerOpenGLWidget::updateWaypoint(int id, const Waypoint &wp)
{
    const std::vector<Waypoint> before = m_waypoints;
    for (auto &waypoint : m_waypoints)
    {
        if (waypoint.sequence() == id)
        {
            waypoint = wp;
            waypoint.setSequence(id); // Preserve sequence number
            commitEdit(before, m_waypoints);
            emit waypointsEdited();
            update();
            return;
        }
    }
}

void PathPlannerOpenGLWidget::removeWaypoint(int id)
{
    const std::vector<Waypoint> before = m_waypoints;
    auto it = std::remove_if(m_waypoints.begin(), m_waypoints.end(),
                             [id](const Waypoint &wp)
                             { return wp.sequence() == id; });

    if (it != m_waypoints.end())
    {
        m_waypoints.erase(it, m_waypoints.end());
        for (int i = 0; i < static_cast<int>(m_waypoints.size()); ++i)
        {
            m_waypoints[i].setSequence(i + 1);
        }

        if (m_selectedWaypoint == id)
        {
            m_selectedWaypoint = -1;
        }
        m_selectedWaypointIds.clear();
        commitEdit(before, m_waypoints);
        emit waypointsEdited();
        update();
    }
}

void PathPlannerOpenGLWidget::clearWaypoints()
{
    const std::vector<Waypoint> before = m_waypoints;
    m_waypoints.clear();
    m_selectedWaypoint = -1;
    m_selectedWaypointIds.clear();
    commitEdit(before, m_waypoints);
    emit waypointsEdited();
    update();
}

void PathPlannerOpenGLWidget::setSelectedWaypoint(int id)
{
    m_selectedWaypoint = id;
    m_selectedWaypointIds.clear();
    if (id >= 0)
        m_selectedWaypointIds.insert(id);
    emit selectionChanged(m_selectedWaypointIds);
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

void PathPlannerOpenGLWidget::setInteractionMode(InteractionMode mode)
{
    if (m_interactionMode != mode)
    {
        m_interactionMode = mode;
        m_activeHandle = TransformHandle::None;
        update();
    }
}

void PathPlannerOpenGLWidget::commitEdit(const std::vector<Waypoint> &before, const std::vector<Waypoint> &after)
{
    if (m_applyingHistory || before == after)
        return;
    clearRedoHistory();
    m_undoStack.append(EditCommand{before, after});
    updateHistorySignals();
}

void PathPlannerOpenGLWidget::clearRedoHistory()
{
    if (!m_redoStack.isEmpty())
        m_redoStack.clear();
}

void PathPlannerOpenGLWidget::updateHistorySignals()
{
    emit editHistoryStateChanged(canUndo(), canRedo());
}

bool PathPlannerOpenGLWidget::undo()
{
    if (m_undoStack.isEmpty())
        return false;
    const EditCommand cmd = m_undoStack.takeLast();
    m_redoStack.append(cmd);
    m_applyingHistory = true;
    m_waypoints = cmd.before;
    m_applyingHistory = false;
    if (m_selectedWaypoint > static_cast<int>(m_waypoints.size()))
        m_selectedWaypoint = -1;
    emit waypointsEdited();
    updateHistorySignals();
    update();
    return true;
}

bool PathPlannerOpenGLWidget::redo()
{
    if (m_redoStack.isEmpty())
        return false;
    const EditCommand cmd = m_redoStack.takeLast();
    m_undoStack.append(cmd);
    m_applyingHistory = true;
    m_waypoints = cmd.after;
    m_applyingHistory = false;
    if (m_selectedWaypoint > static_cast<int>(m_waypoints.size()))
        m_selectedWaypoint = -1;
    emit waypointsEdited();
    updateHistorySignals();
    update();
    return true;
}

bool PathPlannerOpenGLWidget::duplicateSelectedWaypoint()
{
    if (m_selectedWaypoint < 0)
        return false;
    for (const Waypoint &wp : m_waypoints)
    {
        if (wp.sequence() == m_selectedWaypoint)
        {
            QVector3D p(wp.x(), wp.y(), wp.z());
            p.setX(p.x() + 0.4f);
            p.setY(p.y() + 0.4f);
            addWaypoint(p);
            return true;
        }
    }
    return false;
}

bool PathPlannerOpenGLWidget::deleteSelectedWaypoint()
{
    if (m_selectedWaypoint < 0)
        return false;
    removeWaypoint(m_selectedWaypoint);
    return true;
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
    : QWidget(parent), m_mainLayout(nullptr), m_contentLayout(nullptr), m_topBarWidget(nullptr), m_controlsLayout(nullptr), m_openglWidget(nullptr), m_waypointGroup(nullptr), m_viewGroup(nullptr), m_waypointTable(nullptr), m_waypointCountLabel(nullptr), m_pathMenuButton(nullptr), m_uploadMissionButton(nullptr), m_runMissionButton(nullptr), m_cancelMissionButton(nullptr), m_createModeButton(nullptr), m_transformModeButton(nullptr), m_playPathButton(nullptr), m_stopPathButton(nullptr), m_pathNameEdit(nullptr), m_missionStatusLabel(nullptr), m_resetCameraButton(nullptr), m_viewModeButton(nullptr), m_defaultAltitudeSpinBox(nullptr), m_undoEditButton(nullptr), m_redoEditButton(nullptr), m_pathAnimationTimer(nullptr), m_currentAnimationWaypoint(0), m_animationProgress(0.0f), m_isPlayingPath(false), m_selectedWaypoint(-1), m_droneController(nullptr), m_updatingWaypointTable(false)
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
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);
    m_mainLayout->setSpacing(8);

    m_contentLayout = new QHBoxLayout;
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(8);
    m_mainLayout->addLayout(m_contentLayout, 1);

    // Left pane: toolbar + visualizer (toolbar width matches visualizer width).
    QWidget *visualizerPane = new QWidget(this);
    QVBoxLayout *visualizerLayout = new QVBoxLayout(visualizerPane);
    visualizerLayout->setContentsMargins(0, 0, 0, 0);
    visualizerLayout->setSpacing(8);

    setupTopBar();
    visualizerLayout->addWidget(m_topBarWidget);

    // Create OpenGL widget
    m_openglWidget = new PathPlannerOpenGLWidget;
    visualizerLayout->addWidget(m_openglWidget, 1);
    m_contentLayout->addWidget(visualizerPane, 3);

    // Create a scrollable controls panel so newly shown groups do not squash controls.
    QScrollArea *controlsScrollArea = new QScrollArea(this);
    controlsScrollArea->setWidgetResizable(true);
    controlsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlsScrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    QWidget *controlsContainer = new QWidget;
    m_controlsLayout = new QVBoxLayout(controlsContainer);
    m_controlsLayout->setContentsMargins(0, 0, 0, 0);
    m_controlsLayout->setSpacing(8);
    m_controlsLayout->setSizeConstraint(QLayout::SetMinimumSize);

    controlsScrollArea->setWidget(controlsContainer);
    m_contentLayout->addWidget(controlsScrollArea, 1);

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
    connect(m_openglWidget, &PathPlannerOpenGLWidget::waypointMoved,
            this, [this](int, const QVector3D &) {
                updateWaypointTable();
                emitWaypointsChanged();
            });
    connect(m_openglWidget, &PathPlannerOpenGLWidget::waypointsEdited,
            this, [this]() {
                updateWaypointTable();
                emitWaypointsChanged();
            });
    connect(m_openglWidget, &PathPlannerOpenGLWidget::editHistoryStateChanged,
            this, [this](bool, bool) { updateUndoRedoButtons(); });
}

void PathPlannerWidget::setupTopBar()
{
    m_topBarWidget = new QWidget(this);
    m_topBarWidget->setStyleSheet(
        "QWidget { background-color: #2d2d2d; border: none; }");
    QHBoxLayout *topLayout = new QHBoxLayout(m_topBarWidget);
    topLayout->setContentsMargins(4, 3, 4, 3);
    topLayout->setSpacing(4);

    auto makeTopButton = [this](const QString &text) {
        QPushButton *button = new QPushButton(text, m_topBarWidget);
        button->setFixedSize(26, 24);
        button->setFlat(true);
        button->setStyleSheet(
            "QPushButton { "
            "  color: #c7d2de; "
            "  background: transparent; "
            "  border: none; "
            "  border-radius: 0px; "
            "  font-size: 14px; "
            "  padding: 0px; "
            "} "
            "QPushButton:hover { color: #f3f6fb; background-color: rgba(255,255,255,0.07); } "
            "QPushButton:pressed { color: #ffffff; background-color: rgba(255,255,255,0.12); } "
            "QPushButton:checked { color: #3fb1ff; background-color: rgba(63,177,255,0.10); } "
            "QPushButton:disabled { color: #6b7280; background: transparent; }");
        return button;
    };

    auto addSectionSeparator = [topLayout]() {
        QFrame *sep = new QFrame;
        sep->setFrameShape(QFrame::VLine);
        sep->setLineWidth(1);
        sep->setFixedHeight(14);
        sep->setStyleSheet("QFrame { color: #4b5563; background-color: #4b5563; }");
        topLayout->addSpacing(2);
        topLayout->addWidget(sep);
        topLayout->addSpacing(2);
    };

    m_undoEditButton = makeTopButton(QString::fromUtf8("\xE2\x86\xB6"));
    m_redoEditButton = makeTopButton(QString::fromUtf8("\xE2\x86\xB7"));
    m_playPathButton = makeTopButton(QString::fromUtf8("\xE2\x96\xB6"));
    m_stopPathButton = makeTopButton(QString::fromUtf8("\xE2\x96\xA0"));
    m_transformModeButton = makeTopButton(QString::fromUtf8("\xE2\x86\x94"));
    m_createModeButton = makeTopButton(QStringLiteral("+"));
    m_uploadMissionButton = makeTopButton(QString::fromUtf8("\xE2\x86\x91"));
    m_runMissionButton = makeTopButton(QString::fromUtf8("\xE2\x96\xB6"));
    m_cancelMissionButton = makeTopButton(QString::fromUtf8("\xE2\x96\xA0"));
    m_createModeButton->setCheckable(true);
    m_transformModeButton->setCheckable(true);
    m_transformModeButton->setChecked(true);

    m_undoEditButton->setToolTip("Undo");
    m_redoEditButton->setToolTip("Redo");
    m_playPathButton->setToolTip("Play Preview");
    m_stopPathButton->setToolTip("Stop Preview");
    m_createModeButton->setToolTip("Create Mode");
    m_transformModeButton->setToolTip("Transform Mode");
    m_uploadMissionButton->setToolTip("Upload Mission");
    m_runMissionButton->setToolTip("Play Mission");
    m_cancelMissionButton->setToolTip("Stop Mission");
    m_uploadMissionButton->setEnabled(false);
    m_runMissionButton->setEnabled(false);
    m_cancelMissionButton->setEnabled(false);
    m_stopPathButton->setEnabled(false);
    m_undoEditButton->setEnabled(false);
    m_redoEditButton->setEnabled(false);

    m_pathMenuButton = new QToolButton(m_topBarWidget);
    m_pathMenuButton->setText(QString::fromUtf8("\xE2\x89\xA1"));
    m_pathMenuButton->setToolTip("Path actions");
    m_pathMenuButton->setPopupMode(QToolButton::InstantPopup);
    m_pathMenuButton->setFixedSize(26, 24);
    m_pathMenuButton->setStyleSheet(
        "QToolButton { color: #c7d2de; background: transparent; border: none; border-radius: 0px; font-size: 14px; } "
        "QToolButton:hover { color: #f3f6fb; background-color: rgba(255,255,255,0.07); } "
        "QToolButton:pressed { color: #ffffff; background-color: rgba(255,255,255,0.12); } "
        "QToolButton::menu-indicator { image: none; width: 0px; }");
    QMenu *pathMenu = new QMenu(m_pathMenuButton);
    QAction *newPathAction = pathMenu->addAction("New");
    QAction *loadPathAction = pathMenu->addAction("Load");
    QAction *savePathAction = pathMenu->addAction("Save");
    connect(newPathAction, &QAction::triggered, this, [this]() {
        m_pathNameEdit->setText("New Path");
        onClearPath();
    });
    connect(loadPathAction, &QAction::triggered, this, &PathPlannerWidget::onLoadPath);
    connect(savePathAction, &QAction::triggered, this, &PathPlannerWidget::onSavePath);
    m_pathMenuButton->setMenu(pathMenu);
    topLayout->addWidget(m_pathMenuButton);

    m_pathNameEdit = new QLineEdit("New Path", m_topBarWidget);
    m_pathNameEdit->setMinimumWidth(220);
    m_pathNameEdit->setPlaceholderText("Path name");
    m_pathNameEdit->setStyleSheet(
        "QLineEdit { background-color: #2b2f35; color: #e5e7eb; border: 1px solid #4b5563; border-radius: 3px; padding: 3px 8px; } "
        "QLineEdit:focus { border: 1px solid #3b82f6; }");
    topLayout->addWidget(m_pathNameEdit);
    connect(m_pathNameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_waypointGroup)
            m_waypointGroup->setTitle("Waypoints - " + (text.isEmpty() ? QString("New Path") : text));
    });
    // Top bar order:
    // path controls, path name, tools (transform/create), undo/redo,
    // preview controls (play/stop), mission controls (upload/play/stop)
    addSectionSeparator();
    topLayout->addWidget(m_transformModeButton);
    topLayout->addWidget(m_createModeButton);
    addSectionSeparator();
    topLayout->addWidget(m_undoEditButton);
    topLayout->addWidget(m_redoEditButton);
    addSectionSeparator();
    topLayout->addWidget(m_playPathButton);
    topLayout->addWidget(m_stopPathButton);
    addSectionSeparator();
    topLayout->addWidget(m_uploadMissionButton);
    topLayout->addWidget(m_runMissionButton);
    topLayout->addWidget(m_cancelMissionButton);
    topLayout->addStretch();

    m_missionStatusLabel = new QLabel("Status: No mission uploaded", m_topBarWidget);
    m_missionStatusLabel->setStyleSheet("QLabel { color: #9ca3af; border: none; }");
    topLayout->addWidget(m_missionStatusLabel);

    connect(m_createModeButton, &QPushButton::clicked, this, [this]() { onInteractionModeChanged(0); });
    connect(m_transformModeButton, &QPushButton::clicked, this, [this]() { onInteractionModeChanged(1); });
}

void PathPlannerWidget::setupControls()
{
    // Waypoint group
    m_waypointGroup = new QGroupBox("Waypoints - New Path");
    m_waypointGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    m_controlsLayout->addWidget(m_waypointGroup);

    QVBoxLayout *waypointLayout = new QVBoxLayout(m_waypointGroup);
    waypointLayout->setContentsMargins(8, 6, 8, 8);
    waypointLayout->setSpacing(6);

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

    m_controlsLayout->addStretch();

    // Connect signals
    connect(m_uploadMissionButton, &QPushButton::clicked, this, &PathPlannerWidget::onUploadMission);
    connect(m_runMissionButton, &QPushButton::clicked, this, &PathPlannerWidget::onRunMission);
    connect(m_cancelMissionButton, &QPushButton::clicked, this, &PathPlannerWidget::onCancelMission);
    connect(m_playPathButton, &QPushButton::clicked, this, &PathPlannerWidget::onPlayPath);
    connect(m_stopPathButton, &QPushButton::clicked, this, &PathPlannerWidget::onStopPath);
    connect(m_resetCameraButton, &QPushButton::clicked, this, &PathPlannerWidget::onCameraReset);
    connect(m_viewModeButton, &QPushButton::clicked, this, &PathPlannerWidget::onViewModeChanged);
    connect(m_defaultAltitudeSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value)
            { m_openglWidget->setDefaultAltitude(static_cast<float>(value)); });

    connect(m_undoEditButton, &QPushButton::clicked, this, [this]() {
        if (m_openglWidget->undo()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
    connect(m_redoEditButton, &QPushButton::clicked, this, [this]() {
        if (m_openglWidget->redo()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });

    (void) new QShortcut(QKeySequence(Qt::Key_Delete), this, [this]() {
        if (m_openglWidget->deleteSelectedWaypoint()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
    (void) new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this, [this]() {
        if (m_openglWidget->duplicateSelectedWaypoint()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
    (void) new QShortcut(QKeySequence::Undo, this, [this]() {
        if (m_openglWidget->undo()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
    (void) new QShortcut(QKeySequence::Redo, this, [this]() {
        if (m_openglWidget->redo()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
}

void PathPlannerWidget::setupWaypointTable()
{
    m_waypointTable = new QTableWidget;
    m_waypointTable->setColumnCount(5);
    m_waypointTable->setHorizontalHeaderLabels({"X", "Y", "Z", "Yaw", "Hold"});
    m_waypointTable->setMinimumHeight(170);
    m_waypointTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_waypointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_waypointTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_waypointTable->setWordWrap(false);
    m_waypointTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_waypointTable->setDragDropMode(QAbstractItemView::InternalMove);
    m_waypointTable->setDragEnabled(true);
    m_waypointTable->setAcceptDrops(true);
    m_waypointTable->setDropIndicatorShown(true);
    m_waypointTable->setDragDropOverwriteMode(false);
    m_waypointTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_waypointTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_waypointTable->setStyleSheet(
        "QTableWidget { background-color: #1f2937; color: white; border: 1px solid #4b5563; gridline-color: #374151; } "
        "QTableWidget::item { padding: 4px; } "
        "QTableWidget::item:selected { background-color: #3b82f6; } "
        "QHeaderView::section { background-color: #374151; color: white; padding: 4px; border: 1px solid #4b5563; } "
        "QTableWidget QScrollBar:vertical { background: transparent; width: 8px; margin: 28px 3px 6px 0px; border: none; } "
        "QTableWidget QScrollBar::handle:vertical { background: rgba(148, 163, 184, 90); border-radius: 4px; min-height: 24px; } "
        "QTableWidget:hover QScrollBar::handle:vertical { background: rgba(148, 163, 184, 185); } "
        "QTableWidget QScrollBar::add-line:vertical, QTableWidget QScrollBar::sub-line:vertical { height: 0px; } "
        "QTableWidget QScrollBar::add-page:vertical, QTableWidget QScrollBar::sub-page:vertical { background: transparent; } "
        "QTableWidget QScrollBar:horizontal { height: 0px; }");

    // Keep the row-header index as the only waypoint number indicator.
    m_waypointTable->verticalHeader()->setVisible(true);
    m_waypointTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_waypointTable->verticalHeader()->setDefaultSectionSize(28);
    m_waypointTable->verticalHeader()->setMinimumSectionSize(28);
    m_waypointTable->verticalHeader()->setMaximumSectionSize(28);
    m_waypointTable->verticalHeader()->setFixedWidth(30);
    m_waypointTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_waypointTable->setColumnWidth(0, 52); // X
    m_waypointTable->setColumnWidth(1, 52); // Y
    m_waypointTable->setColumnWidth(2, 52); // Z
    m_waypointTable->setColumnWidth(3, 52); // Yaw
    m_waypointTable->setColumnWidth(4, 52); // Hold

    // Insert waypoint table at the top of the waypoints section
    QVBoxLayout *waypointLayout = qobject_cast<QVBoxLayout *>(m_waypointGroup->layout());
    waypointLayout->insertWidget(0, m_waypointTable);

    connect(m_waypointTable, &QTableWidget::cellChanged, this, &PathPlannerWidget::onWaypointCellChanged);
    connect(m_waypointTable, &QTableWidget::currentCellChanged,
            this, [this](int currentRow, int, int, int)
            {
                if (currentRow >= 0 && currentRow < m_waypointTable->rowCount()) {
                    QTableWidgetItem *idItem = m_waypointTable->item(currentRow, 0);
                    if (idItem) {
                        onWaypointSelected(idItem->data(Qt::UserRole).toInt());
                    }
                } });
    connect(m_waypointTable->model(), &QAbstractItemModel::rowsMoved,
            this, &PathPlannerWidget::onWaypointRowsMoved);
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

    // Get path name from top-bar field; must be unique.
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

    const QString pathsDir = plannerPathsDirectory();
    QDir dir(pathsDir);

    const QString sanitizedName = FlightPath::fileBaseFromDisplayName(pathName);
    const QString fileName = dir.absoluteFilePath(sanitizedName + QStringLiteral(".json"));
    if (QFile::exists(fileName))
    {
        QMessageBox::warning(this, "Save Path",
                             QString("A saved waypoint path named '%1' already exists.\nPlease choose a new name.")
                                 .arg(pathName));
        return;
    }

    if (saveToJson(fileName))
    {
        // Update the path name edit field
        m_pathNameEdit->setText(pathName);
        if (m_waypointGroup)
            m_waypointGroup->setTitle("Waypoints - " + pathName);

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
    const QString startDir = plannerPathsDirectory();
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "Load Path",
                                                    startDir,
                                                    "JSON Files (*.json)");

    if (!fileName.isEmpty())
    {
        if (loadFromJson(fileName))
        {
            const QString loadedName = QFileInfo(fileName).baseName().replace('_', ' ');
            m_pathNameEdit->setText(loadedName);
            if (m_waypointGroup)
                m_waypointGroup->setTitle("Waypoints - " + loadedName);
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
    if (row < 0 || row >= m_waypointTable->rowCount())
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
            case 0:
                updated.setX(value);
                break;
            case 1:
                updated.setY(value);
                break;
            case 2:
                updated.setZ(value);
                break;
            case 3:
                updated.setYawAngle(value);
                break;
            case 4:
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

void PathPlannerWidget::onInteractionModeChanged(int index)
{
    const bool createMode = (index == 0);
    PathPlannerOpenGLWidget::InteractionMode mode = createMode
        ? PathPlannerOpenGLWidget::CreateMode
        : PathPlannerOpenGLWidget::TransformMode;

    if (m_createModeButton)
        m_createModeButton->setChecked(createMode);
    if (m_transformModeButton)
        m_transformModeButton->setChecked(!createMode);
    m_openglWidget->setInteractionMode(mode);
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

void PathPlannerWidget::updateWaypointTable()
{
    if (!m_waypointTable || !m_openglWidget)
        return;

    // Block signals to prevent triggering cellChanged during update
    m_updatingWaypointTable = true;
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

            QTableWidgetItem *rowHeaderItem = new QTableWidgetItem(QString::number(i + 1));
            rowHeaderItem->setFlags(rowHeaderItem->flags() & ~Qt::ItemIsEditable);
            m_waypointTable->setVerticalHeaderItem(i, rowHeaderItem);

            QTableWidgetItem *xItem = new QTableWidgetItem(QString::number(wp.x(), 'f', 2));
            xItem->setData(Qt::UserRole, wp.sequence());
            m_waypointTable->setItem(i, 0, xItem);

            // Position and parameters (editable)
            m_waypointTable->setItem(i, 1, new QTableWidgetItem(QString::number(wp.y(), 'f', 2)));
            m_waypointTable->setItem(i, 2, new QTableWidgetItem(QString::number(wp.z(), 'f', 2)));
            m_waypointTable->setItem(i, 3, new QTableWidgetItem(QString::number(wp.yawAngle(), 'f', 1)));
            m_waypointTable->setItem(i, 4, new QTableWidgetItem(QString::number(wp.holdTime(), 'f', 1)));
        }
    }

    // Grow the table with waypoint count, then fall back to a clean vertical scrollbar.
    constexpr int kMaxVisibleRows = 12;
    constexpr int kMinVisibleRows = 4;
    const int rowCount = m_waypointTable->rowCount();
    const int visibleRows = qBound(kMinVisibleRows, rowCount, kMaxVisibleRows);
    const int rowHeight = m_waypointTable->verticalHeader()->defaultSectionSize();
    const int headerHeight = m_waypointTable->horizontalHeader()->height();
    const int frameHeight = m_waypointTable->frameWidth() * 2;
    const int targetHeight = headerHeight + (visibleRows * rowHeight) + frameHeight + 2;
    m_waypointTable->setFixedHeight(targetHeight);

    m_waypointTable->blockSignals(false);
    m_updatingWaypointTable = false;

    updateUndoRedoButtons();
}

void PathPlannerWidget::updateUndoRedoButtons()
{
    if (m_undoEditButton)
        m_undoEditButton->setEnabled(m_openglWidget && m_openglWidget->canUndo());
    if (m_redoEditButton)
        m_redoEditButton->setEnabled(m_openglWidget && m_openglWidget->canRedo());
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

void PathPlannerWidget::onWaypointRowsMoved(const QModelIndex &, int, int, const QModelIndex &, int)
{
    if (m_updatingWaypointTable || !m_openglWidget || !m_waypointTable)
        return;

    const auto &waypoints = m_openglWidget->waypoints();
    if (waypoints.size() < 2 || m_waypointTable->rowCount() < 2)
        return;

    std::vector<Waypoint> reordered;
    reordered.reserve(waypoints.size());

    for (int row = 0; row < m_waypointTable->rowCount(); ++row)
    {
        QTableWidgetItem *idItem = m_waypointTable->item(row, 0);
        if (!idItem)
            continue;
        const int sequence = idItem->data(Qt::UserRole).toInt();
        for (const Waypoint &wp : waypoints)
        {
            if (wp.sequence() == sequence)
            {
                reordered.push_back(wp);
                break;
            }
        }
    }

    if (reordered.size() != waypoints.size() || reordered == waypoints)
        return;

    m_openglWidget->setWaypoints(reordered);
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

    const QFileInfo outInfo(path);
    root["name"] = FlightPath::displayNameFromFileBase(outInfo.baseName());
    const QDateTime now = QDateTime::currentDateTime();
    root["created_at"] = now.toString(Qt::ISODate);
    root["modified_at"] = now.toString(Qt::ISODate);

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
