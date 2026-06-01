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
#include <QApplication>
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
#include <QStyledItemDelegate>
#include <QWidgetAction>
#include <QFormLayout>
#include <QVector2D>
#include <QFrame>
#include <QSignalBlocker>
#include <QCryptographicHash>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

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

// Waypoint / planner "logical" frame — same numbers as saved JSON and the RGB origin gizmo:
//   X = forward (red axis, maps to OpenGL world +Z),
//   Y = lateral-left (green axis, maps to world +X). VOXL Mapper commands use lateral-right,
//       so the controller flips Y when sending plan_to and when displaying mapper poses.
//   Z = up / altitude (blue axis, maps to world +Y).
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

static QJsonArray vector3DListToJson(const QVector<QVector3D> &positions)
{
    QJsonArray array;
    for (const QVector3D &p : positions) {
        QJsonArray item;
        item.append(static_cast<double>(p.x()));
        item.append(static_cast<double>(p.y()));
        item.append(static_cast<double>(p.z()));
        array.append(item);
    }
    return array;
}

static QVector<QVector3D> vector3DListFromJson(const QJsonArray &array)
{
    QVector<QVector3D> positions;
    positions.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonArray item = value.toArray();
        if (item.size() < 3)
            continue;
        positions.append(QVector3D(static_cast<float>(item.at(0).toDouble()),
                                   static_cast<float>(item.at(1).toDouble()),
                                   static_cast<float>(item.at(2).toDouble())));
    }
    return positions;
}

static QJsonArray colorListToJson(const QVector<QColor> &colors)
{
    QJsonArray array;
    for (const QColor &color : colors) {
        QJsonArray item;
        item.append(color.red());
        item.append(color.green());
        item.append(color.blue());
        array.append(item);
    }
    return array;
}

static QVector<QColor> colorListFromJson(const QJsonArray &array)
{
    QVector<QColor> colors;
    colors.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonArray item = value.toArray();
        if (item.size() < 3)
            continue;
        colors.append(QColor(qBound(0, item.at(0).toInt(), 255),
                             qBound(0, item.at(1).toInt(), 255),
                             qBound(0, item.at(2).toInt(), 255)));
    }
    return colors;
}

static QJsonArray indexListToJson(const QVector<quint32> &indices)
{
    QJsonArray array;
    for (quint32 index : indices)
        array.append(static_cast<double>(index));
    return array;
}

static QVector<quint32> indexListFromJson(const QJsonArray &array)
{
    QVector<quint32> indices;
    indices.reserve(array.size());
    for (const QJsonValue &value : array) {
        const double n = value.toDouble(-1.0);
        if (n >= 0.0 && n <= static_cast<double>(std::numeric_limits<quint32>::max()))
            indices.append(static_cast<quint32>(n));
    }
    return indices;
}

class WaypointTableWidget : public QTableWidget
{
public:
    explicit WaypointTableWidget(QWidget *parent = nullptr)
        : QTableWidget(parent)
    {
    }

    std::function<void(int, int, bool)> onRowMoveRequested;

protected:
    enum class DropMode
    {
        None,
        Swap,
        InsertBefore,
        InsertAfter
    };

    void paintEvent(QPaintEvent *event) override
    {
        QTableWidget::paintEvent(event);

        if (!m_draggingRow || m_dragSourceRow < 0)
            return;

        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing, true);

        // "Picked up" source row styling.
        const QModelIndex sourceIndex = model()->index(m_dragSourceRow, 0);
        const QRect sourceRect = visualRect(sourceIndex);
        if (sourceRect.isValid())
        {
            QRect fullRowRect(0, sourceRect.top(), viewport()->width(), sourceRect.height());
            painter.fillRect(fullRowRect.adjusted(1, 1, -1, -1), QColor(255, 255, 255, 22));
            QPen sourcePen(QColor(255, 255, 255, 180));
            sourcePen.setStyle(Qt::DashLine);
            sourcePen.setWidth(1);
            painter.setPen(sourcePen);
            painter.drawRect(fullRowRect.adjusted(0, 0, -1, -1));
        }

        // Floating ghost to make drag feel "picked up".
        if (sourceRect.isValid())
        {
            const int ghostHeight = sourceRect.height();
            const int ghostWidth = qMax(120, viewport()->width() - 18);
            int ghostY = m_lastMousePos.y() - (ghostHeight / 2);
            ghostY = qBound(0, ghostY, qMax(0, viewport()->height() - ghostHeight));
            QRect ghostRect(8, ghostY, ghostWidth, ghostHeight);

            painter.fillRect(ghostRect, QColor(255, 255, 255, 34));
            QPen ghostPen(QColor(255, 255, 255, 235));
            ghostPen.setWidth(1);
            painter.setPen(ghostPen);
            painter.drawRect(ghostRect.adjusted(0, 0, -1, -1));

            painter.setPen(QColor(255, 255, 255, 220));
            painter.drawText(ghostRect.adjusted(8, 0, -8, 0),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             QString("Move row %1").arg(m_dragSourceRow + 1));
        }

        // Drop target styling: swap = border on row, insert = line between rows.
        QPen dropPen(QColor(255, 255, 255, 235));
        dropPen.setWidth(2);
        painter.setPen(dropPen);

        if (m_dropMode == DropMode::Swap && m_hoverRow >= 0)
        {
            const QModelIndex hoverIndex = model()->index(m_hoverRow, 0);
            const QRect hoverRect = visualRect(hoverIndex);
            if (hoverRect.isValid())
            {
                QRect fullRowRect(0, hoverRect.top(), viewport()->width(), hoverRect.height());
                painter.drawRect(fullRowRect.adjusted(1, 1, -2, -2));
            }
        }
        else if ((m_dropMode == DropMode::InsertBefore || m_dropMode == DropMode::InsertAfter) && m_insertLineY >= 0)
        {
            const int y = qBound(0, m_insertLineY, viewport()->height() - 1);
            painter.drawLine(2, y, viewport()->width() - 3, y);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            const QModelIndex index = indexAt(event->pos());
            if (index.isValid())
            {
                setCurrentCell(index.row(), 0);
                selectRow(index.row());
                m_dragSourceRow = index.row();
                m_dragStartPos = event->pos();
                m_lastMousePos = event->pos();
                m_draggingRow = false;
                m_dropMode = DropMode::None;
                m_hoverRow = -1;
                m_insertLineY = -1;
            }
            else
            {
                m_dragSourceRow = -1;
                m_draggingRow = false;
                m_dropMode = DropMode::None;
                m_hoverRow = -1;
                m_insertLineY = -1;
            }
        }
        QTableWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton) || m_dragSourceRow < 0)
        {
            QTableWidget::mouseMoveEvent(event);
            return;
        }

        const int dragThreshold = QApplication::startDragDistance();
        if (!m_draggingRow && (event->pos() - m_dragStartPos).manhattanLength() < dragThreshold)
        {
            return;
        }

        if (!m_draggingRow)
        {
            m_draggingRow = true;
            setCursor(Qt::ClosedHandCursor);
            clearSelection();
        }
        const QPoint pos = event->pos();
        m_lastMousePos = pos;
        updateDropTarget(pos);
        viewport()->update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragSourceRow >= 0 && m_draggingRow)
        {
            updateDropTarget(event->pos());

            if (m_dropMode == DropMode::Swap && m_hoverRow >= 0 && m_hoverRow != m_dragSourceRow)
            {
                if (onRowMoveRequested)
                    onRowMoveRequested(m_dragSourceRow, m_hoverRow, true);
            }
            else if (m_dropMode == DropMode::InsertBefore || m_dropMode == DropMode::InsertAfter)
            {
                int toRow = rowCount();
                if (m_hoverRow >= 0)
                {
                    toRow = m_hoverRow;
                    if (m_dropMode == DropMode::InsertAfter)
                        ++toRow;
                }

                if (toRow > m_dragSourceRow)
                    --toRow;
                toRow = qBound(0, toRow, rowCount() - 1);

                if (toRow != m_dragSourceRow && onRowMoveRequested)
                    onRowMoveRequested(m_dragSourceRow, toRow, false);
            }
        }

        m_dragSourceRow = -1;
        m_draggingRow = false;
        m_dropMode = DropMode::None;
        m_hoverRow = -1;
        m_insertLineY = -1;
        m_lastMousePos = QPoint();
        unsetCursor();
        viewport()->update();
        QTableWidget::mouseReleaseEvent(event);
    }

private:
    void updateDropTarget(const QPoint &pos)
    {
        m_hoverRow = rowAt(pos.y());
        m_dropMode = DropMode::None;
        m_insertLineY = -1;

        if (m_hoverRow < 0 || m_hoverRow >= rowCount())
            return;

        const QModelIndex rowIndex = model()->index(m_hoverRow, 0);
        const QRect rowRect = visualRect(rowIndex);
        if (!rowRect.isValid())
            return;

        const int edgeThresholdPx = 5;
        const int distanceToTop = std::abs(pos.y() - rowRect.top());
        const int distanceToBottom = std::abs(pos.y() - rowRect.bottom());

        if (distanceToTop <= edgeThresholdPx)
        {
            m_dropMode = DropMode::InsertBefore;
            m_insertLineY = rowRect.top();
            return;
        }
        if (distanceToBottom <= edgeThresholdPx)
        {
            m_dropMode = DropMode::InsertAfter;
            m_insertLineY = rowRect.bottom();
            return;
        }

        m_dropMode = DropMode::Swap;
    }

    int m_dragSourceRow = -1;
    QPoint m_dragStartPos;
    QPoint m_lastMousePos;
    bool m_draggingRow = false;
    DropMode m_dropMode = DropMode::None;
    int m_hoverRow = -1;
    int m_insertLineY = -1;
};

class WaypointNoFocusDelegate : public QStyledItemDelegate
{
public:
    explicit WaypointNoFocusDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        opt.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

// Heading in world XZ from logical yaw (degrees). 0° = world +Z (logical +X / forward / red).
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
    : QOpenGLWidget(parent), m_shaderProgram(nullptr), m_cameraPosition(0, 5, 10), m_cameraTarget(0, 0, 0), m_cameraUp(0, 1, 0), m_cameraDistance(15.0f), m_cameraYaw(0.0f), m_cameraPitch(30.0f), m_viewMode(View3DMode), m_orthoZoom(10.0f), m_defaultAltitude(2.0f), m_defaultAcceptanceRadius(0.5f), m_defaultHoldTime(0.0f), m_defaultYawAngle(0.0f), m_dronePositionLogical(0, 0, 0), m_droneYawDeg(0.0f), m_hasDronePose(false), m_selectedWaypoint(-1), m_hoveredWaypoint(-1), m_hoveredSegment(-1), m_mousePressed(false), m_isDragging(false), m_draggingTransform(false), m_activeHandle(TransformHandle::None), m_hasHoverPreview(false), m_pendingCreatePlacement(false), m_createPressOnExistingWaypoint(false), m_applyingHistory(false), m_dragGizmoOriginWorld(), m_dragYawPlaneAngleStartRad(0.0f), m_dragStartYawDeg(0.0f), m_animationTime(0.0f)
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
    drawMapperRenderData();
    drawDroneAxes();
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

/// Yaw handle distance along heading: halfway between axis-handle length (L)
/// and the plane-handle ring radius.
/// Plane handles are placed at (+/-0.45L, +/-0.45L) on a plane, so ring radius is sqrt(2) * 0.45L.
static float gizmoYawTipRadialDistance(float axisLength)
{
    const float axisHandleRadius = axisLength;
    const float planeHandleRadius = std::sqrt(2.0f) * 0.45f * axisLength;
    return 0.5f * (axisHandleRadius + planeHandleRadius);
}

void PathPlannerOpenGLWidget::drawWaypointLabels(QPainter &painter)
{
    // Set up font for labels
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(12);
    painter.setFont(font);

    if (m_hasHoverPreview && effectiveCreate())
    {
        const QPoint ghostPos = worldToScreen(m_lastHoverPreviewWorldPos);
        if (ghostPos.x() >= 0 && ghostPos.x() <= width() && ghostPos.y() >= 0 && ghostPos.y() <= height())
        {
            const float rawRadiusPx = projectedSphereRadiusPxRaw(m_lastHoverPreviewWorldPos, kWaypointSphereRadiusWorld);
            const int previewRadiusPx = qMax(2, static_cast<int>(rawRadiusPx + 0.5f));
            const int previewOutlinePx = qMax(1, static_cast<int>(qBound(1.0f, rawRadiusPx * 0.14f, 3.0f)));
            painter.setPen(QPen(QColor(180, 220, 255, 220), 2, Qt::DashLine));
            painter.setPen(QPen(QColor(180, 220, 255, 220), previewOutlinePx, Qt::DashLine));
            painter.setBrush(QColor(60, 120, 180, 55));
            painter.drawEllipse(ghostPos, previewRadiusPx, previewRadiusPx);
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

        const bool homeWp = wp.isMapperHome();
        QVector3D rgb;
        if (wp.sequence() == m_selectedWaypoint)
            rgb = homeWp ? QVector3D(1.0f, 0.88f, 0.45f) : QVector3D(0.56f, 0.76f, 1.0f);
        else if (m_selectedWaypointIds.contains(wp.sequence()))
            rgb = homeWp ? QVector3D(1.0f, 0.78f, 0.35f) : QVector3D(0.2f, 0.72f, 0.95f);
        else if (wp.sequence() == m_hoveredWaypoint)
            rgb = homeWp ? QVector3D(1.0f, 0.72f, 0.28f) : QVector3D(0.38f, 0.65f, 1.0f);
        else
            rgb = homeWp ? QVector3D(0.95f, 0.62f, 0.15f) : QVector3D(0.12f, 0.42f, 0.92f);

        WaypointHudEntry e;
        e.screenCenter = screenPos;
        e.zCenterNdc = zC;
        e.fillColor = QColor::fromRgbF(rgb.x(), rgb.y(), rgb.z());
        e.indexText = homeWp ? QStringLiteral("H") : QString::number(wp.sequence());
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

    // Live handle value chip while dragging transform controls.
    if (m_draggingTransform && m_selectedWaypoint >= 0 && m_activeHandle != TransformHandle::None)
    {
        const QVector3D p = selectedWaypointLogicalPosition();
        QString chipText;
        switch (m_activeHandle)
        {
        case TransformHandle::AxisX:
            chipText = QString("X: %1 m").arg(p.x(), 0, 'f', 2);
            break;
        case TransformHandle::AxisY:
            chipText = QString("Y: %1 m").arg(p.y(), 0, 'f', 2);
            break;
        case TransformHandle::AxisZ:
            chipText = QString("Z: %1 m").arg(p.z(), 0, 'f', 2);
            break;
        case TransformHandle::PlaneXY:
            chipText = QString("X: %1 m   Y: %2 m").arg(p.x(), 0, 'f', 2).arg(p.y(), 0, 'f', 2);
            break;
        case TransformHandle::PlaneXZ:
            chipText = QString("X: %1 m   Z: %2 m").arg(p.x(), 0, 'f', 2).arg(p.z(), 0, 'f', 2);
            break;
        case TransformHandle::PlaneYZ:
            chipText = QString("Y: %1 m   Z: %2 m").arg(p.y(), 0, 'f', 2).arg(p.z(), 0, 'f', 2);
            break;
        case TransformHandle::Yaw:
            chipText = QString("Yaw: %1%2").arg(selectedWaypointYawDeg(), 0, 'f', 1).arg(QChar(0x00B0));
            break;
        case TransformHandle::None:
            break;
        }

        if (!chipText.isEmpty())
        {
            QFont chipFont = painter.font();
            chipFont.setBold(true);
            chipFont.setPointSize(10);
            painter.setFont(chipFont);
            const QFontMetrics fmChip(chipFont);

            const int padX = 8;
            const int padY = 4;
            const int chipW = fmChip.horizontalAdvance(chipText) + (padX * 2);
            const int chipH = fmChip.height() + (padY * 2);
            const QPoint anchor = worldToScreen(gizmoCenterWorld());

            int chipX = anchor.x() + 14;
            int chipY = anchor.y() - chipH - 14;
            const int margin = 8;
            chipX = qBound(margin, chipX, width() - chipW - margin);
            chipY = qBound(margin, chipY, height() - chipH - margin);
            const QRect chipRect(chipX, chipY, chipW, chipH);

            painter.setPen(QPen(QColor(90, 99, 112), 1));
            painter.setBrush(QColor(26, 31, 40, 220));
            painter.drawRoundedRect(chipRect, 4, 4);
            painter.setPen(QColor(229, 231, 235));
            painter.drawText(chipRect.adjusted(padX, padY, -padX, -padY), Qt::AlignLeft | Qt::AlignVCenter, chipText);
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
    
    // Logical X (forward, red): world +Z
    generateCylinderVertices(QVector3D(0, 0, 0), QVector3D(0, 0, axisLength), axisRadius,
                             QVector3D(1.0f, 0.0f, 0.0f), axesVertices, axesColors);
    
    // Logical Y (lateral, green): world +X — default view reads like screen-left vs forward/up
    generateCylinderVertices(QVector3D(0, 0, 0), QVector3D(axisLength, 0, 0), axisRadius,
                             QVector3D(0.0f, 1.0f, 0.0f), axesVertices, axesColors);
    
    // Logical Z (up, blue): world +Y (vertical)
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

void PathPlannerOpenGLWidget::drawDroneAxes()
{
    if (!m_hasDronePose)
        return;

    const QVector3D origin = logicalToWorld(m_dronePositionLogical);
    const float yawRad = qDegreesToRadians(m_droneYawDeg);
    const QVector3D forwardWorld(std::sin(yawRad), 0.0f, std::cos(yawRad));
    const QVector3D rightWorld(std::cos(yawRad), 0.0f, -std::sin(yawRad));
    const QVector3D upWorld(0.0f, 1.0f, 0.0f);
    constexpr float kAxisLength = 0.65f;

    QVector<float> vertices;
    QVector<float> colors;
    appendLine(origin, origin + forwardWorld * kAxisLength, QVector3D(1.0f, 0.1f, 0.1f), vertices, colors);
    appendLine(origin, origin + rightWorld * kAxisLength, QVector3D(0.1f, 1.0f, 0.1f), vertices, colors);
    appendLine(origin, origin + upWorld * kAxisLength, QVector3D(0.2f, 0.45f, 1.0f), vertices, colors);
    appendSphere(origin, 0.12f, QVector3D(0.95f, 0.95f, 0.95f), vertices, colors);

    m_vao.bind();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices.data(), vertices.size() * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    QOpenGLBuffer colorBuffer;
    colorBuffer.create();
    colorBuffer.bind();
    colorBuffer.allocate(colors.data(), colors.size() * sizeof(float));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);

    glLineWidth(4.0f);
    glDrawArrays(GL_LINES, 0, 6);
    glLineWidth(1.0f);
    glDrawArrays(GL_TRIANGLES, 6, (vertices.size() / 3) - 6);

    colorBuffer.release();
    m_vertexBuffer.release();
    m_vao.release();
}

void PathPlannerOpenGLWidget::drawMapperRenderData()
{
    if (m_mapperRenderPositionsLogical.isEmpty() && m_mapperMeshPositionsLogical.isEmpty())
        return;

    QVector<float> vertices;
    QVector<float> colors;
    GLenum drawMode = GL_POINTS;

    if (!m_mapperMeshPositionsLogical.isEmpty() && !m_mapperMeshTriangleIndices.isEmpty()) {
        vertices.reserve(m_mapperMeshTriangleIndices.size() * 3);
        colors.reserve(m_mapperMeshTriangleIndices.size() * 3);
        drawMode = GL_TRIANGLES;

        for (quint32 index : m_mapperMeshTriangleIndices) {
            if (index >= static_cast<quint32>(m_mapperMeshPositionsLogical.size()))
                continue;
            const int i = static_cast<int>(index);
            const QVector3D world = logicalToWorld(m_mapperMeshPositionsLogical.at(i));
            const QColor color = (i < m_mapperMeshColors.size() && m_mapperMeshColors.at(i).isValid())
                                     ? m_mapperMeshColors.at(i)
                                     : QColor(110, 160, 210);

            vertices << world.x() << world.y() << world.z();
            colors << static_cast<float>(color.redF())
                   << static_cast<float>(color.greenF())
                   << static_cast<float>(color.blueF());
        }
    } else {
        const QVector<QVector3D> &positions = m_mapperMeshPositionsLogical.isEmpty()
                                                  ? m_mapperRenderPositionsLogical
                                                  : m_mapperMeshPositionsLogical;
        const QVector<QColor> &sourceColors = m_mapperMeshPositionsLogical.isEmpty()
                                                  ? m_mapperRenderColors
                                                  : m_mapperMeshColors;
        vertices.reserve(positions.size() * 3);
        colors.reserve(positions.size() * 3);

        for (int i = 0; i < positions.size(); ++i) {
            const QVector3D world = logicalToWorld(positions.at(i));
            const QColor color = (i < sourceColors.size() && sourceColors.at(i).isValid())
                                     ? sourceColors.at(i)
                                     : QColor(110, 160, 210);

            vertices << world.x() << world.y() << world.z();
            colors << static_cast<float>(color.redF())
                   << static_cast<float>(color.greenF())
                   << static_cast<float>(color.blueF());
        }
    }

    if (vertices.isEmpty())
        return;

    m_vao.bind();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices.data(), vertices.size() * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    QOpenGLBuffer colorBuffer;
    colorBuffer.create();
    colorBuffer.bind();
    colorBuffer.allocate(colors.data(), colors.size() * sizeof(float));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);

    glPointSize(drawMode == GL_POINTS ? 3.0f : 1.0f);
    glDrawArrays(drawMode, 0, vertices.size() / 3);
    glPointSize(1.0f);

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
        const bool homeWp = wp.isMapperHome();
        QVector3D color;

        if (wp.sequence() == m_selectedWaypoint)
        {
            color = homeWp ? QVector3D(1.0f, 0.88f, 0.45f) : QVector3D(0.56f, 0.76f, 1.0f);
        }
        else if (m_selectedWaypointIds.contains(wp.sequence()))
        {
            color = homeWp ? QVector3D(1.0f, 0.78f, 0.35f) : QVector3D(0.2f, 0.72f, 0.95f);
        }
        else if (wp.sequence() == m_hoveredWaypoint)
        {
            color = homeWp ? QVector3D(1.0f, 0.72f, 0.28f) : QVector3D(0.38f, 0.65f, 1.0f);
        }
        else
        {
            color = homeWp ? QVector3D(0.95f, 0.62f, 0.15f) : QVector3D(0.12f, 0.42f, 0.92f);
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

// Gizmo handles (world space at waypoint): logical X -> world +Z (red/forward), Y -> world +X (green/lateral),
// Z -> world +Y (blue/up). Orange = logical XY, purple = XZ, cyan = YZ. Gold = yaw in horizontal plane (deg).
void PathPlannerOpenGLWidget::drawGizmo()
{
    if (m_selectedWaypoint < 0 || !effectiveTransform())
        return;
    if (selectedWaypointIsMapperHome())
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
    appendSphere(xyCenter, handleRadius * 0.8f, axisColor(TransformHandle::PlaneXY, QVector3D(1.0f, 0.6f, 0.2f)), handleVertices, handleColors);
    if (m_viewMode != TopDownMode)
    {
        const QVector3D xzCenter = center + QVector3D(0.0f, 0.45f * axisLength, 0.45f * axisLength);
        const QVector3D yzCenter = center + QVector3D(0.45f * axisLength, 0.45f * axisLength, 0.0f);
        appendSphere(xzCenter, handleRadius * 0.8f, axisColor(TransformHandle::PlaneXZ, QVector3D(0.8f, 0.3f, 1.0f)), handleVertices, handleColors);
        appendSphere(yzCenter, handleRadius * 0.8f, axisColor(TransformHandle::PlaneYZ, QVector3D(0.2f, 1.0f, 1.0f)), handleVertices, handleColors);
    }

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
        m_createPressOnExistingWaypoint = false;
        const Qt::KeyboardModifiers mods = event->modifiers();

        // Gizmo handles take priority over create-placement and waypoint picking.
        if (effectiveTransform() && m_selectedWaypoint >= 0 && !selectedWaypointIsMapperHome())
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
            // Clicking an existing waypoint in Create mode is a selection action,
            // not a new-placement action. Clear any pending create preview.
            m_pendingCreatePlacement = false;
            m_hasHoverPreview = false;
            if (effectiveCreate())
                m_createPressOnExistingWaypoint = true;

            if ((mods & Qt::ControlModifier) && effectiveTransform())
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

        if (effectiveCreate() && (mods & Qt::ControlModifier) && m_waypoints.size() > 1)
        {
            QVector3D insertWorld;
            const int segmentIndex = findSegmentAt(event->pos(), &insertWorld, 14.0f);
            if (segmentIndex >= 0)
            {
                const std::vector<Waypoint> before = m_waypoints;
                Waypoint wp(worldToLogical(insertWorld));
                m_waypoints.insert(m_waypoints.begin() + segmentIndex + 1, wp);
                normalizeMapperHomeAndRenumberSequences();
                m_selectedWaypoint = m_waypoints[static_cast<size_t>(segmentIndex) + 1].sequence();
                m_selectedWaypointIds.clear();
                m_selectedWaypointIds.insert(m_selectedWaypoint);
                commitEdit(before, m_waypoints);
                emit waypointSelected(m_selectedWaypoint);
                emit waypointsEdited();
                update();
                return;
            }
        }

        const bool shouldCreate = effectiveCreate();
        if (shouldCreate)
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
            m_pendingCreatePlacement = true;
            m_createPressOnExistingWaypoint = false;
            update();
            return;
        }

        // Clear selection on empty click only when transform is on and create is off
        // (otherwise create mode uses the click for placement).
        if (effectiveTransform() && !effectiveCreate())
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
    if (effectiveCreate() && m_pendingCreatePlacement &&
        !m_createPressOnExistingWaypoint && !(event->buttons() & Qt::RightButton))
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
    if (event->button() == Qt::LeftButton)
    {
        if (effectiveCreate() && m_pendingCreatePlacement && !m_createPressOnExistingWaypoint)
        {
            addWaypoint(worldToLogical(m_lastHoverPreviewWorldPos));
        }
        m_pendingCreatePlacement = false;
        m_createPressOnExistingWaypoint = false;
        m_hasHoverPreview = false;
    }

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
        {TransformHandle::Yaw, worldToScreen(yawTip)}
    };
    if (m_viewMode != TopDownMode)
    {
        points.append({TransformHandle::PlaneXZ, worldToScreen(center + QVector3D(0.0f, 0.45f * axisLength, 0.45f * axisLength))});
        points.append({TransformHandle::PlaneYZ, worldToScreen(center + QVector3D(0.45f * axisLength, 0.45f * axisLength, 0.0f))});
    }

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

bool PathPlannerOpenGLWidget::selectedWaypointIsMapperHome() const
{
    for (const Waypoint &wp : m_waypoints) {
        if (wp.sequence() == m_selectedWaypoint)
            return wp.isMapperHome();
    }
    return false;
}

bool PathPlannerOpenGLWidget::updateWaypointYawAngle(int id, float yawDeg)
{
    yawDeg = wrapYawDegrees180(yawDeg);
    for (Waypoint &waypoint : m_waypoints)
    {
        if (waypoint.sequence() == id)
        {
            if (waypoint.isMapperHome())
                return false;
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
            if (waypoint.isMapperHome())
                return false;
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

void PathPlannerOpenGLWidget::normalizeMapperHomeAndRenumberSequences()
{
    int homeIndex = -1;
    for (int i = 0; i < static_cast<int>(m_waypoints.size()); ++i) {
        if (!m_waypoints[i].isMapperHome())
            continue;
        if (homeIndex < 0) {
            homeIndex = i;
        } else {
            m_waypoints[i].setAsMapperHome(false);
        }
    }
    if (homeIndex > 0) {
        Waypoint h = m_waypoints[homeIndex];
        m_waypoints.erase(m_waypoints.begin() + homeIndex);
        m_waypoints.insert(m_waypoints.begin(), h);
    }
    int nextSeq = 1;
    for (Waypoint &wp : m_waypoints) {
        if (wp.isMapperHome())
            wp.setSequence(0);
        else
            wp.setSequence(nextSeq++);
    }
}

void PathPlannerOpenGLWidget::setWaypoints(const std::vector<Waypoint> &waypoints)
{
    m_waypoints = waypoints;
    normalizeMapperHomeAndRenumberSequences();
    if (m_selectedWaypoint >= 0) {
        bool found = false;
        for (const Waypoint &wp : m_waypoints) {
            if (wp.sequence() == m_selectedWaypoint) {
                found = true;
                break;
            }
        }
        if (!found)
            m_selectedWaypoint = -1;
    }
    update();
}

void PathPlannerOpenGLWidget::addWaypoint(const QVector3D &point)
{
    const std::vector<Waypoint> before = m_waypoints;

    int newSequence = 1;
    if (!m_waypoints.empty()) {
        int maxSeq = 0;
        for (const Waypoint &w : m_waypoints)
            maxSeq = qMax(maxSeq, w.sequence());
        newSequence = maxSeq + 1;
    }

    Waypoint wp(point);
    wp.setSequence(newSequence);
    wp.setAcceptanceRadius(m_defaultAcceptanceRadius);
    wp.setHoldTime(m_defaultHoldTime);
    wp.setYawAngle(m_defaultYawAngle);
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
            if (waypoint.isMapperHome())
                return;
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
        normalizeMapperHomeAndRenumberSequences();

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

void PathPlannerOpenGLWidget::setDronePoseLogical(const QVector3D &positionLogical, float yawDeg)
{
    m_dronePositionLogical = positionLogical;
    m_droneYawDeg = yawDeg;
    m_hasDronePose = true;
    update();
}

void PathPlannerOpenGLWidget::setMapperRenderData(const QVector<QVector3D> &positionsLogical, const QVector<QColor> &colors)
{
    m_mapperRenderPositionsLogical = positionsLogical;
    m_mapperRenderColors = colors;
    update();
}

void PathPlannerOpenGLWidget::setMapperMeshData(const QVector<QVector3D> &positionsLogical, const QVector<QColor> &colors, const QVector<quint32> &triangleIndices)
{
    const int previousVertexCount = m_mapperMeshPositionsLogical.size();

    m_mapperMeshPositionsLogical = positionsLogical;
    m_mapperMeshColors = colors;

    if (!triangleIndices.isEmpty()) {
        m_mapperMeshTriangleIndices = triangleIndices;
    } else if (positionsLogical.isEmpty()) {
        m_mapperMeshTriangleIndices.clear();
    } else if (positionsLogical.size() == previousVertexCount && !m_mapperMeshTriangleIndices.isEmpty()) {
        // Mapper sometimes streams vertex refreshes without resending indices; keep drawing triangles.
    } else {
        m_mapperMeshTriangleIndices.clear();
    }

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

bool PathPlannerOpenGLWidget::effectiveCreate() const
{
    return !m_navigationOnly && m_createToolEnabled;
}

bool PathPlannerOpenGLWidget::effectiveTransform() const
{
    return !m_navigationOnly && m_transformToolEnabled;
}

void PathPlannerOpenGLWidget::setNavigationOnly(bool on)
{
    if (m_navigationOnly == on)
        return;
    m_navigationOnly = on;
    m_activeHandle = TransformHandle::None;
    m_draggingTransform = false;
    m_pendingCreatePlacement = false;
    m_hasHoverPreview = false;
    update();
}

void PathPlannerOpenGLWidget::setEditorTools(bool createEnabled, bool transformEnabled)
{
    if (m_createToolEnabled == createEnabled && m_transformToolEnabled == transformEnabled)
        return;
    m_createToolEnabled = createEnabled;
    m_transformToolEnabled = transformEnabled;
    m_activeHandle = TransformHandle::None;
    m_draggingTransform = false;
    update();
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
    normalizeMapperHomeAndRenumberSequences();
    if (m_selectedWaypoint >= 0) {
        bool found = false;
        for (const Waypoint &wp : m_waypoints) {
            if (wp.sequence() == m_selectedWaypoint) {
                found = true;
                break;
            }
        }
        if (!found)
            m_selectedWaypoint = -1;
    }
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
    normalizeMapperHomeAndRenumberSequences();
    if (m_selectedWaypoint >= 0) {
        bool found = false;
        for (const Waypoint &wp : m_waypoints) {
            if (wp.sequence() == m_selectedWaypoint) {
                found = true;
                break;
            }
        }
        if (!found)
            m_selectedWaypoint = -1;
    }
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
            if (wp.isMapperHome())
                return false;
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
    : QWidget(parent), m_mainLayout(nullptr), m_contentLayout(nullptr), m_topBarWidget(nullptr), m_controlsLayout(nullptr), m_topBarPreUploadToolbar(nullptr), m_topBarEditingCluster(nullptr), m_topBarMissionCluster(nullptr), m_topBarReturnToEditButton(nullptr), m_topBarLandButton(nullptr), m_topBarRtlButton(nullptr), m_topBarForceDisarmButton(nullptr), m_topBarFlightTermButton(nullptr), m_openglWidget(nullptr), m_waypointGroup(nullptr), m_viewGroup(nullptr), m_waypointTable(nullptr), m_waypointCountLabel(nullptr), m_waypointDefaultsButton(nullptr), m_defaultAcceptanceRadiusSpinBox(nullptr), m_defaultHoldSpinBox(nullptr), m_defaultYawSpinBox(nullptr), m_pathMenuButton(nullptr), m_uploadMissionButton(nullptr), m_missionPlayButton(nullptr), m_missionPauseContinueButton(nullptr), m_createModeButton(nullptr), m_transformModeButton(nullptr), m_playPathPreviewButton(nullptr), m_stopPathPreviewButton(nullptr), m_pathNameEdit(nullptr), m_missionStatusLabel(nullptr), m_resetCameraButton(nullptr), m_viewModeButton(nullptr), m_defaultAltitudeSpinBox(nullptr), m_undoEditButton(nullptr), m_redoEditButton(nullptr), m_pathPreviewAnimationTimer(nullptr), m_pathPreviewWaypointIndex(0), m_pathPreviewProgress(0.0f), m_isPlayingPathPreview(false), m_selectedWaypoint(-1), m_droneController(nullptr), m_hasUploadedSnapshot(false), m_waypointsDirtySinceUpload(false), m_editingLocked(false), m_updatingWaypointTable(false), m_reorderingWaypointRows(false), m_rightPanelLayout(nullptr)
{
    setupUI();

    m_pathPreviewAnimationTimer = new QTimer(this);
    m_pathPreviewAnimationTimer->setInterval(50);
    connect(m_pathPreviewAnimationTimer, &QTimer::timeout, this, &PathPlannerWidget::onPathPreviewAnimationTimer);
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

    // Compact in-canvas view selector (top-right of visualizer).
    m_viewModeButton = new QPushButton("Top-Down", m_openglWidget);
    m_viewModeButton->setCursor(Qt::PointingHandCursor);
    m_viewModeButton->setFixedHeight(24);
    m_viewModeButton->setStyleSheet(
        "QPushButton { background-color: rgba(35, 41, 51, 190); color: #e5e7eb; "
        "border: 1px solid rgba(107, 114, 128, 180); border-radius: 4px; padding: 2px 8px; font-size: 11px; } "
        "QPushButton:hover { background-color: rgba(55, 65, 81, 210); }");
    connect(m_viewModeButton, &QPushButton::clicked, this, &PathPlannerWidget::onViewModeChanged);
    m_openglWidget->installEventFilter(this);
    updateViewTogglePlacement();
    m_viewModeButton->raise();

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

    // Wrap the scroll area in a right-panel widget so the VIO group can be
    // pinned to the bottom outside the scroll area.
    QWidget *rightPanel = new QWidget(this);
    m_rightPanelLayout = new QVBoxLayout(rightPanel);
    m_rightPanelLayout->setContentsMargins(0, 0, 0, 0);
    m_rightPanelLayout->setSpacing(0);
    m_rightPanelLayout->addWidget(controlsScrollArea, 1);
    m_contentLayout->addWidget(rightPanel, 1);

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
    updateMissionChrome();
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

    // Mission toolbar: tinted icons (not plain gray); no toggle “checked” styling. Avoid emoji codepoints (e.g. U+2B07) that paint a colored blob on Windows.
    auto makeMissionToolbarButton = [this](const QString &text, const QString &fgNormal, const QString &fgHover,
                                           int fontSizePx = 14, int fontWeight = 400) {
        QPushButton *button = new QPushButton(text, m_topBarWidget);
        button->setFixedSize(26, 24);
        button->setFlat(true);
        button->setCheckable(false);
        button->setAutoDefault(false);
        button->setDefault(false);
        button->setFocusPolicy(Qt::NoFocus);
        button->setStyleSheet(
            QStringLiteral(
                "QPushButton { color: %1; background: transparent; border: none; border-radius: 2px; "
                "font-size: %3px; font-weight: %4; padding: 0px; } "
                "QPushButton:hover { color: %2; background-color: rgba(255,255,255,0.08); } "
                "QPushButton:pressed { color: #ffffff; background-color: rgba(255,255,255,0.14); } "
                "QPushButton:disabled { color: #525a66; background: transparent; }")
                .arg(fgNormal)
                .arg(fgHover)
                .arg(fontSizePx)
                .arg(fontWeight));
        return button;
    };

    auto makeThinSeparator = [](QBoxLayout *lay) {
        QFrame *sep = new QFrame;
        sep->setFrameShape(QFrame::VLine);
        sep->setLineWidth(1);
        sep->setFixedHeight(14);
        sep->setStyleSheet("QFrame { color: #4b5563; background-color: #4b5563; }");
        lay->addSpacing(2);
        lay->addWidget(sep);
        lay->addSpacing(2);
    };

    m_undoEditButton = makeTopButton(QString::fromUtf8("\xE2\x86\xB6"));
    m_redoEditButton = makeTopButton(QString::fromUtf8("\xE2\x86\xB7"));
    m_playPathPreviewButton = makeTopButton(QString::fromUtf8("\xE2\x96\xB6"));
    m_stopPathPreviewButton = makeTopButton(QString::fromUtf8("\xE2\x96\xA0"));
    m_transformModeButton = makeTopButton(QString::fromUtf8("\xE2\x86\x94"));
    m_createModeButton = makeTopButton(QStringLiteral("+"));
    m_uploadMissionButton = makeTopButton(QString::fromUtf8("\xE2\x86\x91"));
    m_topBarReturnToEditButton =
        makeMissionToolbarButton(QString::fromUtf8("\xE2\x86\xA9"), QStringLiteral("#cbd5e1"), QStringLiteral("#f8fafc"));
    m_missionPlayButton =
        makeMissionToolbarButton(QString::fromUtf8("\xE2\x96\xB6"), QStringLiteral("#5eead4"), QStringLiteral("#ccfbf1"));
    // Same glyph as preview stop (U+25A0): avoids Windows emoji-style ⏸ with blue tile background.
    m_missionPauseContinueButton =
        makeMissionToolbarButton(QString::fromUtf8("\xE2\x96\xA0"), QStringLiteral("#5eead4"), QStringLiteral("#ccfbf1"));
    m_topBarLandButton =
        makeMissionToolbarButton(QString::fromUtf8("\xE2\x86\x93"), QStringLiteral("#7dd3fc"), QStringLiteral("#e0f2fe"));
    m_topBarRtlButton = makeMissionToolbarButton(QString(QChar(0x2302)), QStringLiteral("#c4b5fd"),
                                                 QStringLiteral("#ede9fe"), 15, 650);

    m_topBarForceDisarmButton = new QPushButton(QStringLiteral("!"), m_topBarWidget);
    m_topBarForceDisarmButton->setFixedSize(26, 24);
    m_topBarForceDisarmButton->setFlat(true);
    m_topBarForceDisarmButton->setCheckable(false);
    m_topBarForceDisarmButton->setAutoDefault(false);
    m_topBarForceDisarmButton->setDefault(false);
    m_topBarForceDisarmButton->setFocusPolicy(Qt::NoFocus);
    m_topBarForceDisarmButton->setStyleSheet(
        "QPushButton { color: #fecaca; background-color: rgba(127,29,29,0.5); border: 1px solid #dc2626; border-radius: 3px; "
        "font-size: 13px; font-weight: 700; padding: 0px; } "
        "QPushButton:hover { color: #ffffff; background-color: rgba(220,38,38,0.72); border-color: #f87171; } "
        "QPushButton:pressed { background-color: rgba(153,27,27,0.9); } "
        "QPushButton:disabled { color: #5c6570; background-color: rgba(40,40,40,0.35); border-color: #4b5563; }");
    m_topBarFlightTermButton = new QPushButton(QStringLiteral("T"), m_topBarWidget);
    m_topBarFlightTermButton->setFixedSize(26, 24);
    m_topBarFlightTermButton->setFlat(true);
    m_topBarFlightTermButton->setCheckable(false);
    m_topBarFlightTermButton->setAutoDefault(false);
    m_topBarFlightTermButton->setDefault(false);
    m_topBarFlightTermButton->setFocusPolicy(Qt::NoFocus);
    m_topBarFlightTermButton->setStyleSheet(
        "QPushButton { color: #fecaca; background-color: rgba(69,10,10,0.65); border: 1px solid #7f1d1d; border-radius: 3px; "
        "font-size: 11px; font-weight: 800; padding: 0px; } "
        "QPushButton:hover { color: #ffffff; background-color: rgba(127,29,29,0.85); border-color: #dc2626; } "
        "QPushButton:pressed { background-color: rgba(69,10,10,0.95); } "
        "QPushButton:disabled { color: #5c6570; background-color: rgba(40,40,40,0.35); border-color: #4b5563; }");
    m_createModeButton->setCheckable(true);
    m_transformModeButton->setCheckable(true);
    m_transformModeButton->setChecked(true);

    m_undoEditButton->setToolTip("Undo");
    m_redoEditButton->setToolTip("Redo");
    m_playPathPreviewButton->setToolTip("Preview path (local playback)");
    m_stopPathPreviewButton->setToolTip("Stop path preview");
    m_createModeButton->setToolTip("Create waypoints (independent; works together with Transform)");
    m_transformModeButton->setToolTip("Transform: show gizmo and drag handles on the selected waypoint");
    m_uploadMissionButton->setToolTip("Upload Mission");
    m_topBarReturnToEditButton->setToolTip("Return to Edit (unlock path; re-upload required after changes)");
    m_missionPlayButton->setToolTip("Start Mission");
    m_missionPauseContinueButton->setToolTip("Pause Mission (hover in place)");
    m_topBarLandButton->setToolTip("Land");
    m_topBarRtlButton->setToolTip("Return to Launch");
    m_topBarForceDisarmButton->setToolTip(QStringLiteral("Force disarm (MAVLink kill motors)"));
    m_topBarFlightTermButton->setToolTip(QStringLiteral("Flight termination (PX4; strongest software stop if enabled)"));
    m_uploadMissionButton->setEnabled(false);
    m_missionPlayButton->setEnabled(false);
    m_missionPauseContinueButton->setEnabled(false);
    m_undoEditButton->setEnabled(false);
    m_redoEditButton->setEnabled(false);
    m_stopPathPreviewButton->setEnabled(false);
    m_topBarLandButton->setEnabled(false);
    m_topBarRtlButton->setEnabled(false);
    m_topBarForceDisarmButton->setEnabled(false);
    m_topBarFlightTermButton->setEnabled(false);

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
    pathMenu->addSeparator();
    QAction *loadMapperMapAction = pathMenu->addAction("Load VOXL Map...");
    QAction *uploadMapperMapAction = pathMenu->addAction("Upload Mesh Export File...");
    QAction *saveMapperMapAction = pathMenu->addAction("Save VOXL Map...");
    QAction *clearMapperMapAction = pathMenu->addAction("Clear VOXL Map");
    QAction *planHomeAction = pathMenu->addAction("Plan VOXL Home (place H at drone pose)");
    planHomeAction->setToolTip(QStringLiteral(
        "Sends plan_home to voxl-mapper (home goal is fixed in mapper at (0,0,-1.5) per ModalAI docs — see voxl-mapper README). "
        "Places read-only waypoint H at the current mapper pose in this UI only; it is not uploaded as a mission point."));
    connect(newPathAction, &QAction::triggered, this, [this]() {
        m_pathNameEdit->setText("New Path");
        onClearPath();
    });
    connect(loadPathAction, &QAction::triggered, this, &PathPlannerWidget::onLoadPath);
    connect(savePathAction, &QAction::triggered, this, &PathPlannerWidget::onSavePath);
    connect(loadMapperMapAction, &QAction::triggered, this, [this]() {
        if (!m_droneController || !m_droneController->isConnected()) {
            QMessageBox::warning(this, "Load VOXL Map", "Connect to VOXL before loading a mapper map.");
            return;
        }
        bool ok = false;
        const QString path = QInputDialog::getText(this, "Load VOXL Map",
                                                   "Map directory on the VOXL filesystem (blank uses /data/voxl_mapper):",
                                                   QLineEdit::Normal,
                                                   m_mapperMapPath,
                                                   &ok);
        if (!ok)
            return;
        m_mapperMapPath = path.trimmed();
        clearMapperVisualization();
        m_droneController->loadMapperMap(m_mapperMapPath);
    });
    connect(uploadMapperMapAction, &QAction::triggered, this, [this]() {
        if (!m_droneController || !m_droneController->isConnected()) {
            QMessageBox::warning(this, "Upload Mesh Export", "Connect to VOXL before uploading a mesh export file.");
            return;
        }

        const QString localPath = QFileDialog::getOpenFileName(this,
                                                               "Upload Mesh Export File",
                                                               QString(),
                                                               "Mesh export files (*.ply *.obj *.gltf *.glb);;All files (*.*)");
        if (localPath.isEmpty())
            return;

        const QFileInfo fileInfo(localPath);
        const QString defaultRemotePath = QStringLiteral("/data/voxl_mapper/%1").arg(fileInfo.fileName());

        bool ok = false;
        const QString remotePath = QInputDialog::getText(this, "Upload Mesh Export",
                                                         "Destination mesh file path on the VOXL filesystem:",
                                                         QLineEdit::Normal,
                                                         defaultRemotePath,
                                                         &ok);
        if (!ok || remotePath.trimmed().isEmpty())
            return;

        m_droneController->uploadMapperMap(localPath, remotePath.trimmed());
    });
    connect(saveMapperMapAction, &QAction::triggered, this, [this]() {
        if (!m_droneController || !m_droneController->isConnected()) {
            QMessageBox::warning(this, "Save VOXL Map", "Connect to VOXL before saving a mapper map.");
            return;
        }
        bool ok = false;
        const QString path = QInputDialog::getText(this, "Save VOXL Map",
                                                   "Map directory on the VOXL filesystem (blank uses /data/voxl_mapper):",
                                                   QLineEdit::Normal,
                                                   m_mapperMapPath,
                                                   &ok);
        if (!ok)
            return;
        m_mapperMapPath = path.trimmed();
        m_droneController->saveMapperMap(QStringLiteral("ply"), m_mapperMapPath);
    });
    connect(clearMapperMapAction, &QAction::triggered, this, [this]() {
        if (!m_droneController || !m_droneController->isConnected()) {
            QMessageBox::warning(this, "Clear VOXL Map", "Connect to VOXL before clearing the mapper map.");
            return;
        }
        if (QMessageBox::question(this, "Clear VOXL Map",
                "Clear the current VOXL Mapper map?") == QMessageBox::Yes) {
            m_droneController->clearMapperMap();
            clearMapperVisualization();
        }
    });
    connect(planHomeAction, &QAction::triggered, this, [this]() {
        if (!m_droneController || !m_droneController->isConnected()) {
            QMessageBox::warning(this, QStringLiteral("Plan VOXL Home"),
                                 QStringLiteral("Connect to the drone first."));
            return;
        }
        if (!m_openglWidget) {
            return;
        }
        QVector3D p;
        float yawDeg = 0.0f;
        if (!m_droneController->mapperPoseAvailableForPlanner(p, yawDeg)) {
            QMessageBox::warning(this, QStringLiteral("Plan VOXL Home"),
                                 QStringLiteral("No mapper pose yet. Wait for the VOXL mapper stream, then try again."));
            return;
        }
        m_droneController->planMapperHome();

        Waypoint home(p);
        home.setAsMapperHome(true);
        home.setSequence(0);
        home.setYawAngle(yawDeg);
        home.setAcceptanceRadius(m_openglWidget->defaultAcceptanceRadius());
        home.setHoldTime(0.0f);

        std::vector<Waypoint> wps = m_openglWidget->waypoints();
        if (!wps.empty() && wps.front().isMapperHome())
            wps[0] = home;
        else
            wps.insert(wps.begin(), home);
        m_openglWidget->setWaypoints(wps);
        updateWaypointTable();
        onWaypointSelected(0);
        emitWaypointsChanged();
        m_lastMissionStatusText = QStringLiteral("plan_home sent; H marks current pose (read-only, not in mission upload)");
        updateMissionChrome();
    });
    m_pathMenuButton->setMenu(pathMenu);

    m_pathNameEdit = new QLineEdit("New Path", m_topBarWidget);
    m_pathNameEdit->setMinimumWidth(220);
    m_pathNameEdit->setPlaceholderText("Path name");
    m_pathNameEdit->setStyleSheet(
        "QLineEdit { background-color: #2b2f35; color: #e5e7eb; border: 1px solid #4b5563; border-radius: 3px; padding: 3px 8px; } "
        "QLineEdit:focus { border: 1px solid #3b82f6; }");
    connect(m_pathNameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_waypointGroup)
            m_waypointGroup->setTitle("Waypoints - " + (text.isEmpty() ? QString("New Path") : text));
    });

    m_topBarEditingCluster = new QWidget(m_topBarWidget);
    QHBoxLayout *editLay = new QHBoxLayout(m_topBarEditingCluster);
    editLay->setContentsMargins(0, 0, 0, 0);
    editLay->setSpacing(4);
    editLay->addWidget(m_pathMenuButton);
    editLay->addWidget(m_pathNameEdit);
    makeThinSeparator(editLay);
    editLay->addWidget(m_transformModeButton);
    editLay->addWidget(m_createModeButton);
    makeThinSeparator(editLay);
    editLay->addWidget(m_undoEditButton);
    editLay->addWidget(m_redoEditButton);

    m_topBarPreUploadToolbar = new QWidget(m_topBarWidget);
    QHBoxLayout *preUploadLay = new QHBoxLayout(m_topBarPreUploadToolbar);
    preUploadLay->setContentsMargins(0, 0, 0, 0);
    preUploadLay->setSpacing(4);
    preUploadLay->addWidget(m_topBarEditingCluster);
    makeThinSeparator(preUploadLay);
    preUploadLay->addWidget(m_playPathPreviewButton);
    preUploadLay->addWidget(m_stopPathPreviewButton);
    makeThinSeparator(preUploadLay);
    preUploadLay->addWidget(m_uploadMissionButton);

    m_topBarMissionCluster = new QWidget(m_topBarWidget);
    QHBoxLayout *missionLay = new QHBoxLayout(m_topBarMissionCluster);
    missionLay->setContentsMargins(0, 0, 0, 0);
    missionLay->setSpacing(4);

    missionLay->addWidget(m_topBarReturnToEditButton);
    makeThinSeparator(missionLay);
    missionLay->addWidget(m_missionPlayButton);
    missionLay->addWidget(m_missionPauseContinueButton);
    makeThinSeparator(missionLay);
    missionLay->addWidget(m_topBarLandButton);
    missionLay->addWidget(m_topBarRtlButton);
    missionLay->addWidget(m_topBarForceDisarmButton);
    missionLay->addWidget(m_topBarFlightTermButton);

    topLayout->addWidget(m_topBarPreUploadToolbar);
    topLayout->addWidget(m_topBarMissionCluster);
    m_topBarMissionCluster->hide();

    topLayout->addStretch();

    m_missionStatusLabel = new QLabel(m_topBarWidget);
    m_missionStatusLabel->setTextFormat(Qt::RichText);
    m_missionStatusLabel->setStyleSheet("QLabel { color: #9ca3af; border: none; font-size: 11px; }");
    m_missionStatusLabel->setWordWrap(false);
    m_missionStatusLabel->setMaximumWidth(520);
    m_missionStatusLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    topLayout->addWidget(m_missionStatusLabel, 0, Qt::AlignVCenter);

    connect(m_createModeButton, &QPushButton::toggled, this, &PathPlannerWidget::applyEditorToolsFromButtons);
    connect(m_transformModeButton, &QPushButton::toggled, this, &PathPlannerWidget::applyEditorToolsFromButtons);
    connect(m_topBarReturnToEditButton, &QPushButton::clicked, this, &PathPlannerWidget::onReturnToEdit);
    connect(m_missionPlayButton, &QPushButton::clicked, this, &PathPlannerWidget::onMissionPlayClicked);
    connect(m_missionPauseContinueButton, &QPushButton::clicked, this, &PathPlannerWidget::onMissionPauseContinueClicked);
    connect(m_topBarLandButton, &QPushButton::clicked, this, &PathPlannerWidget::onLandMission);
    connect(m_topBarRtlButton, &QPushButton::clicked, this, &PathPlannerWidget::onReturnToLaunchMission);
    connect(m_topBarForceDisarmButton, &QPushButton::clicked, this, &PathPlannerWidget::onForceDisarmMission);
    connect(m_topBarFlightTermButton, &QPushButton::clicked, this, &PathPlannerWidget::onFlightTerminationMission);
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

    m_waypointDefaultsButton = new QToolButton(m_waypointGroup);
    m_waypointDefaultsButton->setToolTip("Defaults for new waypoints");
    m_waypointDefaultsButton->setText("...");
    m_waypointDefaultsButton->setFixedSize(30, 28);
    m_waypointDefaultsButton->setPopupMode(QToolButton::InstantPopup);
    m_waypointDefaultsButton->setStyleSheet(
        "QToolButton { color: #d1d5db; background: transparent; border: none; font-size: 13px; font-weight: bold; } "
        "QToolButton:hover { color: #f3f6fb; background-color: rgba(255,255,255,0.07); } "
        "QToolButton::menu-indicator { image: none; width: 0px; }");

    QMenu *defaultsMenu = new QMenu(m_waypointDefaultsButton);
    defaultsMenu->setStyleSheet(
        "QMenu { background-color: #262b31; border: 1px solid #4b5563; }");

    QWidgetAction *defaultsPanelAction = new QWidgetAction(defaultsMenu);
    QWidget *defaultsPanel = new QWidget(defaultsMenu);
    QVBoxLayout *defaultsPanelLayout = new QVBoxLayout(defaultsPanel);
    defaultsPanelLayout->setContentsMargins(10, 10, 10, 8);
    defaultsPanelLayout->setSpacing(6);

    QFormLayout *defaultsForm = new QFormLayout;
    defaultsForm->setContentsMargins(0, 0, 0, 0);
    defaultsForm->setSpacing(6);
    defaultsForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    defaultsForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_defaultAltitudeSpinBox = new QDoubleSpinBox(defaultsPanel);
    m_defaultAltitudeSpinBox->setRange(0.0, 100.0);
    m_defaultAltitudeSpinBox->setDecimals(2);
    m_defaultAltitudeSpinBox->setSingleStep(0.5);
    m_defaultAltitudeSpinBox->setSuffix(" m");
    m_defaultAltitudeSpinBox->setValue(2.0);

    m_defaultAcceptanceRadiusSpinBox = new QDoubleSpinBox(defaultsPanel);
    m_defaultAcceptanceRadiusSpinBox->setRange(0.10, 20.0);
    m_defaultAcceptanceRadiusSpinBox->setDecimals(2);
    m_defaultAcceptanceRadiusSpinBox->setSingleStep(0.10);
    m_defaultAcceptanceRadiusSpinBox->setSuffix(" m");
    m_defaultAcceptanceRadiusSpinBox->setValue(0.5);

    m_defaultHoldSpinBox = new QDoubleSpinBox(defaultsPanel);
    m_defaultHoldSpinBox->setRange(0.0, 120.0);
    m_defaultHoldSpinBox->setDecimals(1);
    m_defaultHoldSpinBox->setSingleStep(0.5);
    m_defaultHoldSpinBox->setSuffix(" s");
    m_defaultHoldSpinBox->setValue(2.0);

    m_defaultYawSpinBox = new QDoubleSpinBox(defaultsPanel);
    m_defaultYawSpinBox->setRange(-180.0, 180.0);
    m_defaultYawSpinBox->setDecimals(1);
    m_defaultYawSpinBox->setSingleStep(5.0);
    m_defaultYawSpinBox->setSuffix(QString::fromUtf8("\xC2\xB0"));
    m_defaultYawSpinBox->setValue(0.0);

    defaultsForm->addRow("Altitude", m_defaultAltitudeSpinBox);
    defaultsForm->addRow("Acceptance", m_defaultAcceptanceRadiusSpinBox);
    defaultsForm->addRow("Hold", m_defaultHoldSpinBox);
    defaultsForm->addRow("Yaw", m_defaultYawSpinBox);
    defaultsPanelLayout->addLayout(defaultsForm);

    QLabel *defaultsHintLabel = new QLabel("Applies to new waypoints only.", defaultsPanel);
    defaultsHintLabel->setStyleSheet("QLabel { color: #9ca3af; font-size: 11px; }");
    defaultsPanelLayout->addWidget(defaultsHintLabel);

    defaultsPanelAction->setDefaultWidget(defaultsPanel);
    defaultsMenu->addAction(defaultsPanelAction);
    m_waypointDefaultsButton->setMenu(defaultsMenu);

    if (m_openglWidget)
    {
        m_openglWidget->setDefaultAltitude(static_cast<float>(m_defaultAltitudeSpinBox->value()));
        m_openglWidget->setDefaultAcceptanceRadius(static_cast<float>(m_defaultAcceptanceRadiusSpinBox->value()));
        m_openglWidget->setDefaultHoldTime(static_cast<float>(m_defaultHoldSpinBox->value()));
        m_openglWidget->setDefaultYawAngle(static_cast<float>(m_defaultYawSpinBox->value()));
    }

    m_controlsLayout->addStretch();

    // VIO Reset — single button that resets both OV Extended and QVIO.
    {
        const QString groupStyle =
            QStringLiteral("QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; "
                           "margin-top: 1ex; padding-top: 10px; } "
                           "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
        const QString btnStyle =
            QStringLiteral("QPushButton { background: #b91c1c; color: white; border: none; "
                           "padding: 6px 14px; border-radius: 4px; font-weight: bold; } "
                           "QPushButton:hover { background: #991b1b; } "
                           "QPushButton:disabled { background: #4b5563; color: #6b7280; }");

        QGroupBox *vioGroup = new QGroupBox(QStringLiteral("VIO Reset"), this);
        vioGroup->setStyleSheet(groupStyle);
        QVBoxLayout *vioGroupLayout = new QVBoxLayout(vioGroup);
        vioGroupLayout->setContentsMargins(6, 8, 6, 8);
        vioGroupLayout->setSpacing(6);

        QLabel *desc = new QLabel(
            QStringLiteral("Resets both Open-VINS and QVIO services. "
                           "VIO will be interrupted for several seconds — "
                           "do not reset during an active flight."),
            vioGroup);
        desc->setWordWrap(true);
        desc->setStyleSheet(QStringLiteral("QLabel { color: #9ca3af; font-size: 11px; }"));

        QPushButton *btn = new QPushButton(QStringLiteral("Reset VIO"), vioGroup);
        btn->setStyleSheet(btnStyle);
        btn->setFixedHeight(30);

        connect(btn, &QPushButton::clicked, this, [this]() {
            const auto ans = QMessageBox::question(
                this,
                QStringLiteral("Reset VIO"),
                QStringLiteral("Reset VIO on the drone?<br><br>"
                               "Both Open-VINS and QVIO will be restarted. "
                               "VIO will be interrupted for several seconds. "
                               "Do not reset during an active flight."));
            if (ans == QMessageBox::Yes) {
                emit vioResetRequested(QStringLiteral("voxl-open-vins-server"));
                emit vioResetRequested(QStringLiteral("voxl-qvio-server"));
            }
        });

        vioGroupLayout->addWidget(desc);
        vioGroupLayout->addWidget(btn);
        m_rightPanelLayout->addWidget(vioGroup);
    }

    // Connect signals
    connect(m_uploadMissionButton, &QPushButton::clicked, this, &PathPlannerWidget::onUploadMission);
    connect(m_playPathPreviewButton, &QPushButton::clicked, this, &PathPlannerWidget::onPlayPathPreview);
    connect(m_stopPathPreviewButton, &QPushButton::clicked, this, &PathPlannerWidget::onStopPathPreview);
    connect(m_defaultAltitudeSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value)
            { m_openglWidget->setDefaultAltitude(static_cast<float>(value)); });
    connect(m_defaultAcceptanceRadiusSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value)
            { m_openglWidget->setDefaultAcceptanceRadius(static_cast<float>(value)); });
    connect(m_defaultHoldSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value)
            { m_openglWidget->setDefaultHoldTime(static_cast<float>(value)); });
    connect(m_defaultYawSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value)
            { m_openglWidget->setDefaultYawAngle(static_cast<float>(value)); });

    connect(m_undoEditButton, &QPushButton::clicked, this, [this]() {
        if (m_editingLocked)
            return;
        if (m_openglWidget->undo()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
    connect(m_redoEditButton, &QPushButton::clicked, this, [this]() {
        if (m_editingLocked)
            return;
        if (m_openglWidget->redo()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });

    (void) new QShortcut(QKeySequence(Qt::Key_Delete), this, [this]() {
        if (m_editingLocked)
            return;
        if (m_openglWidget->deleteSelectedWaypoint()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
    (void) new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this, [this]() {
        if (m_editingLocked)
            return;
        if (m_openglWidget->duplicateSelectedWaypoint()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
    (void) new QShortcut(QKeySequence::Undo, this, [this]() {
        if (m_editingLocked)
            return;
        if (m_openglWidget->undo()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
    (void) new QShortcut(QKeySequence::Redo, this, [this]() {
        if (m_editingLocked)
            return;
        if (m_openglWidget->redo()) {
            updateWaypointTable();
            emitWaypointsChanged();
        }
    });
}

void PathPlannerWidget::setupWaypointTable()
{
    m_waypointTable = new WaypointTableWidget;
    m_waypointTable->setItemDelegate(new WaypointNoFocusDelegate(m_waypointTable));
    m_waypointTable->setColumnCount(5);
    m_waypointTable->setHorizontalHeaderLabels({"X", "Y", "Z", "Yaw", "Hold"});
    m_waypointTable->setMinimumHeight(170);
    m_waypointTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_waypointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_waypointTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_waypointTable->setWordWrap(false);
    m_waypointTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_waypointTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_waypointTable->setDragEnabled(false);
    m_waypointTable->setAcceptDrops(false);
    m_waypointTable->setDropIndicatorShown(false);
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
    if (m_waypointDefaultsButton)
    {
        m_waypointDefaultsButton->setParent(m_waypointTable);
        m_waypointDefaultsButton->setText("...");
        m_waypointDefaultsButton->setCursor(Qt::PointingHandCursor);

        const auto placeDefaultsButton = [this]() {
            if (!m_waypointTable || !m_waypointDefaultsButton)
                return;
            const int x = m_waypointTable->frameWidth();
            const int y = m_waypointTable->frameWidth();
            const int w = m_waypointTable->verticalHeader()->width();
            const int h = m_waypointTable->horizontalHeader()->height();
            m_waypointDefaultsButton->setGeometry(x, y, w, h);
            m_waypointDefaultsButton->raise();
            m_waypointDefaultsButton->show();
        };

        placeDefaultsButton();
        connect(m_waypointTable->horizontalHeader(), &QHeaderView::geometriesChanged, this, placeDefaultsButton);
        connect(m_waypointTable->verticalHeader(), &QHeaderView::geometriesChanged, this, placeDefaultsButton);
    }
    m_waypointTable->setColumnWidth(0, 52); // X
    m_waypointTable->setColumnWidth(1, 52); // Y
    m_waypointTable->setColumnWidth(2, 52); // Z
    m_waypointTable->setColumnWidth(3, 52); // Yaw
    m_waypointTable->setColumnWidth(4, 52); // Hold

    // Insert waypoint table at the top of the waypoints section.
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
    WaypointTableWidget *waypointDragTable = static_cast<WaypointTableWidget *>(m_waypointTable);
    waypointDragTable->onRowMoveRequested = [this](int fromRow, int toRow, bool swapRows) {
        if (m_updatingWaypointTable || !m_openglWidget || !m_waypointTable)
            return;

        const auto &waypoints = m_openglWidget->waypoints();
        if (fromRow < 0 || toRow < 0 ||
            fromRow >= static_cast<int>(waypoints.size()) ||
            toRow >= static_cast<int>(waypoints.size()) ||
            fromRow == toRow)
            return;

        m_reorderingWaypointRows = true;
        std::vector<Waypoint> reordered = waypoints;
        if (swapRows)
        {
            std::swap(reordered[fromRow], reordered[toRow]);
        }
        else
        {
            Waypoint moved = reordered[fromRow];
            reordered.erase(reordered.begin() + fromRow);
            reordered.insert(reordered.begin() + toRow, moved);
        }
        m_openglWidget->setWaypoints(reordered);
        updateWaypointTable();
        emitWaypointsChanged();
        m_reorderingWaypointRows = false;
    };
}

void PathPlannerWidget::onClearPath()
{
    if (!m_openglWidget)
        return;

    m_mapperMapBundleDir.clear();

    if (m_isPlayingPathPreview)
        stopPathPreviewAnimation();

    m_openglWidget->clearWaypoints();
    m_selectedWaypoint = -1;

    if (m_waypointTable)
        updateWaypointTable();

    emitWaypointsChanged();
}

void PathPlannerWidget::removeMapperHomeWaypointAndRefresh()
{
    if (!m_openglWidget)
        return;
    std::vector<Waypoint> wps = m_openglWidget->waypoints();
    const size_t oldCount = wps.size();
    wps.erase(std::remove_if(wps.begin(), wps.end(), [](const Waypoint &w) { return w.isMapperHome(); }), wps.end());
    if (wps.size() == oldCount)
        return;
    const bool clearSelection = (m_selectedWaypoint == 0);
    m_openglWidget->setWaypoints(wps);
    if (clearSelection)
        onWaypointSelected(-1);
    else
        m_openglWidget->setSelectedWaypoint(m_selectedWaypoint);
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

    if (m_plannerPathsDir.trimmed().isEmpty() || m_roomId.trimmed().isEmpty())
    {
        QMessageBox::warning(this, "Save Path",
                             "Select or create a room in Saved Paths before saving a trajectory.");
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

    const QString pathsDir = m_plannerPathsDir.isEmpty() ? plannerPathsDirectory() : m_plannerPathsDir;
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

    QVector<QVector3D> points;
    for (const auto &wp : m_openglWidget->waypoints())
        points.append(QVector3D(wp.x(), wp.y(), wp.z()));

    const QString roomMapName = FlightPath::fileBaseFromDisplayName(
        m_roomName.trimmed().isEmpty() ? m_roomId : m_roomName);
    const QString bundleFolderName = QStringLiteral("../map/mapper_map");
    const QString localBundleAbsPath = QDir(m_roomMapDir).filePath(QStringLiteral("mapper_map"));
    const QString remotePkg = QStringLiteral("/data/voxl_mapper/missions/%1").arg(roomMapName);

    const bool canBundle = m_droneController && m_droneController->isConnected()
                           && m_droneController->isMapperMeshConnected();

    if (canBundle) {
        m_mapperMapPath = remotePkg;
        if (!m_roomMapDir.trimmed().isEmpty()) {
            QDir roomMapDir(m_roomMapDir);
            roomMapDir.removeRecursively();
            QDir().mkpath(m_roomMapDir);
        }
        if (!saveToJson(fileName, bundleFolderName)) {
            QMessageBox::warning(this, "Save Path", "Failed to save path.");
            return;
        }

        m_mapperMapBundleDir.clear();
        m_pathNameEdit->setText(pathName);
        if (m_waypointGroup)
            m_waypointGroup->setTitle("Waypoints - " + pathName);
        emit pathSaved(pathName, points);

        m_backgroundBundleJsonPath = fileName;
        m_backgroundBundleFolderName = bundleFolderName;

        m_droneController->saveMapperMap(QStringLiteral("ply"), remotePkg);

        if (m_missionStatusLabel) {
            m_missionStatusLabel->setText(
                QStringLiteral("<span style=\"color:#93c5fd;font-weight:600;\">Path saved — copying mapper map from "
                               "VOXL (scp)…</span>"));
            m_missionStatusLabel->setStyleSheet(QStringLiteral("QLabel { border: none; font-size: 11px; }"));
        }

        QTimer::singleShot(4000, this, [this, localBundleAbsPath, remotePkg]() {
            if (!m_droneController)
                return;
            m_droneController->downloadMapperMapFromVehicle(localBundleAbsPath, remotePkg);
        });

        QMessageBox::information(
            this,
            "Save Path",
            QString("Path saved to:\n%1\n\nThe room map was cleared locally and is copying from VOXL into:\n%2\n\n"
                    "You will get another message when that finishes (or if it fails).")
                .arg(fileName, localBundleAbsPath));
        return;
    }

    if (!saveToJson(fileName))
    {
        QMessageBox::warning(this, "Save Path", "Failed to save path.");
        return;
    }

    m_mapperMapBundleDir.clear();

    m_pathNameEdit->setText(pathName);
    if (m_waypointGroup)
        m_waypointGroup->setTitle("Waypoints - " + pathName);
    emit pathSaved(pathName, points);

    const QString extra = QStringLiteral(
        "\n\nMapper map was not bundled (connect to the drone with the mapper mesh socket open to save the map next "
        "to this file automatically).");
    QMessageBox::information(this, "Save Path",
                             QString("Path '%1' saved successfully to:\n%2%3")
                                 .arg(pathName, fileName, extra));
}

void PathPlannerWidget::onLoadPath()
{
    const QString startDir = m_plannerPathsDir.isEmpty() ? plannerPathsDirectory() : m_plannerPathsDir;
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "Load Path",
                                                    startDir,
                                                    "JSON Files (*.json)");

    if (!fileName.isEmpty())
        loadPathFromFile(fileName, true);
}

void PathPlannerWidget::clearMapperVisualization()
{
    if (!m_openglWidget)
        return;
    m_openglWidget->setMapperRenderData({}, {});
    m_openglWidget->setMapperMeshData({}, {}, {});
}

bool PathPlannerWidget::loadPathFromFile(const QString &fileName, bool showSuccessDialog)
{
    clearMapperVisualization();

    if (!loadFromJson(fileName)) {
        QMessageBox::warning(this, "Load Path", "Failed to load path.");
        return false;
    }

    const QString loadedName = QFileInfo(fileName).baseName().replace('_', ' ');
    m_pathNameEdit->setText(loadedName);
    if (m_waypointGroup)
        m_waypointGroup->setTitle("Waypoints - " + loadedName);

    if (m_droneController && m_droneController->isConnected()) {
        const bool hasLocalRoomMap = !m_mapperMapBundleDir.isEmpty() && QDir(m_mapperMapBundleDir).exists();
        if (hasLocalRoomMap) {
            const QString remotePath = m_mapperMapPath.trimmed().isEmpty()
                                           ? QStringLiteral("/data/voxl_mapper/missions/%1").arg(
                                                 FlightPath::fileBaseFromDisplayName(
                                                     m_roomName.trimmed().isEmpty() ? loadedName : m_roomName))
                                           : m_mapperMapPath.trimmed();
            m_mapperMapPath = remotePath;
            m_droneController->restoreMapperMapFromBundle(m_mapperMapBundleDir, remotePath);
            if (m_missionStatusLabel) {
                m_missionStatusLabel->setText(
                    QStringLiteral("<span style=\"color:#93c5fd;font-weight:600;\">Restoring saved room map to VOXL…</span>"));
                m_missionStatusLabel->setStyleSheet(QStringLiteral("QLabel { border: none; font-size: 11px; }"));
            }
        } else if (!m_mapperMapPath.isEmpty()) {
            QMessageBox::warning(this,
                                 "Load Associated VOXL Map",
                                 QString("This trajectory references a room map, but the saved local map folder is missing:\n%1\n\n"
                                         "The waypoints were loaded, but the app did not pull a replacement map from VOXL.")
                                     .arg(m_mapperMapBundleDir.isEmpty() ? QStringLiteral("(no local map folder recorded)")
                                                                         : m_mapperMapBundleDir));
        }
    }

    if (showSuccessDialog)
        QMessageBox::information(this, "Load Path", "Path loaded successfully!");
    return true;
}

void PathPlannerWidget::onWaypointSelected(int id)
{
    m_selectedWaypoint = id;
    m_openglWidget->setSelectedWaypoint(id);

    if (!m_waypointTable || id < 0) {
        if (m_waypointTable)
            m_waypointTable->clearSelection();
        return;
    }

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
    if (m_editingLocked || m_updatingWaypointTable || m_reorderingWaypointRows)
        return;

    if (row < 0 || row >= m_waypointTable->rowCount())
        return;

    QTableWidgetItem *idItem = m_waypointTable->item(row, 0);
    if (!idItem)
        return;

    int id = idItem->data(Qt::UserRole).toInt();
    if (id < 0)
        return;

    // Find the waypoint and update it
    const auto &waypoints = m_openglWidget->waypoints();
    for (const auto &wp : waypoints)
    {
        if (wp.sequence() == id)
        {
            if (wp.isMapperHome())
                return;
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

bool PathPlannerWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_openglWidget && event &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        updateViewTogglePlacement();
    }
    return QWidget::eventFilter(watched, event);
}

void PathPlannerWidget::updateViewTogglePlacement()
{
    if (!m_openglWidget || !m_viewModeButton)
        return;

    const int margin = 8;
    const int buttonWidth = 92;
    const int x = qMax(margin, m_openglWidget->width() - buttonWidth - margin);
    const int y = margin;
    m_viewModeButton->setGeometry(x, y, buttonWidth, 24);
}

void PathPlannerWidget::onViewModeChanged()
{
    if (m_openglWidget->viewMode() == PathPlannerOpenGLWidget::TopDownMode)
    {
        m_openglWidget->setViewMode(PathPlannerOpenGLWidget::View3DMode);
        m_viewModeButton->setText("Top-Down");
    }
    else
    {
        m_openglWidget->setViewMode(PathPlannerOpenGLWidget::TopDownMode);
        m_viewModeButton->setText("3D View");
    }
}

void PathPlannerWidget::applyEditorToolsFromButtons()
{
    if (m_editingLocked || !m_openglWidget)
        return;
    m_openglWidget->setEditorTools(m_createModeButton->isChecked(), m_transformModeButton->isChecked());
}

void PathPlannerWidget::onPlayPathPreview()
{
    if (!m_isPlayingPathPreview && m_openglWidget && !m_openglWidget->waypoints().empty())
        startPathPreviewAnimation();
}

void PathPlannerWidget::onStopPathPreview()
{
    if (m_isPlayingPathPreview)
        stopPathPreviewAnimation();
}

void PathPlannerWidget::onPathPreviewAnimationTimer()
{
    m_pathPreviewProgress += 0.02f;
    if (m_pathPreviewProgress >= 1.0f)
    {
        m_pathPreviewProgress = 0.0f;
        m_pathPreviewWaypointIndex++;

        const auto &waypoints = m_openglWidget->waypoints();
        if (m_pathPreviewWaypointIndex >= static_cast<int>(waypoints.size()))
        {
            stopPathPreviewAnimation();
            return;
        }
    }

    if (m_openglWidget)
        m_openglWidget->update();
}

void PathPlannerWidget::updateWaypointTable()
{
    if (!m_waypointTable || !m_openglWidget)
        return;

    m_reorderingWaypointRows = false;

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

            QTableWidgetItem *rowHeaderItem = new QTableWidgetItem(wp.isMapperHome() ? QStringLiteral("H")
                                                                                     : QString::number(static_cast<int>(i) + 1));
            rowHeaderItem->setFlags(rowHeaderItem->flags() & ~Qt::ItemIsEditable);
            m_waypointTable->setVerticalHeaderItem(i, rowHeaderItem);

            const Qt::ItemFlags cellFlags = wp.isMapperHome()
                ? (Qt::ItemIsSelectable | Qt::ItemIsEnabled)
                : (Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
            QTableWidgetItem *xItem = new QTableWidgetItem(QString::number(wp.x(), 'f', 2));
            xItem->setData(Qt::UserRole, wp.sequence());
            xItem->setFlags(cellFlags);
            m_waypointTable->setItem(i, 0, xItem);

            // Position and parameters (editable except mapper home row)
            auto *yItem = new QTableWidgetItem(QString::number(wp.y(), 'f', 2));
            yItem->setFlags(cellFlags);
            m_waypointTable->setItem(i, 1, yItem);
            auto *zItem = new QTableWidgetItem(QString::number(wp.z(), 'f', 2));
            zItem->setFlags(cellFlags);
            m_waypointTable->setItem(i, 2, zItem);
            auto *yawItem = new QTableWidgetItem(QString::number(wp.yawAngle(), 'f', 1));
            yawItem->setFlags(cellFlags);
            m_waypointTable->setItem(i, 3, yawItem);
            auto *holdItem = new QTableWidgetItem(QString::number(wp.holdTime(), 'f', 1));
            holdItem->setFlags(cellFlags);
            m_waypointTable->setItem(i, 4, holdItem);
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
        m_undoEditButton->setEnabled(!m_editingLocked && m_openglWidget && m_openglWidget->canUndo());
    if (m_redoEditButton)
        m_redoEditButton->setEnabled(!m_editingLocked && m_openglWidget && m_openglWidget->canRedo());
}

void PathPlannerWidget::startPathPreviewAnimation()
{
    m_isPlayingPathPreview = true;
    m_pathPreviewWaypointIndex = 0;
    m_pathPreviewProgress = 0.0f;
    if (m_pathPreviewAnimationTimer)
        m_pathPreviewAnimationTimer->start();

    if (m_playPathPreviewButton)
        m_playPathPreviewButton->setEnabled(false);
    if (m_stopPathPreviewButton)
        m_stopPathPreviewButton->setEnabled(true);
}

void PathPlannerWidget::stopPathPreviewAnimation()
{
    m_isPlayingPathPreview = false;
    if (m_pathPreviewAnimationTimer)
        m_pathPreviewAnimationTimer->stop();

    if (m_playPathPreviewButton)
        m_playPathPreviewButton->setEnabled(true);
    if (m_stopPathPreviewButton)
        m_stopPathPreviewButton->setEnabled(false);
}

void PathPlannerWidget::loadPoints(const QVector<QVector3D> &points)
{
    clearMapperVisualization();
    m_mapperMapPath.clear();
    m_mapperMapBundleDir.clear();

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
        onWaypointSelected(waypoints.front().sequence());
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
    if (m_editingLocked || m_updatingWaypointTable || !m_openglWidget || !m_waypointTable)
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
    updateDirtyState();
    emit waypointsChanged(m_openglWidget->waypoints());
    updateMissionChrome();
}

QString PathPlannerWidget::waypointFingerprint(const std::vector<Waypoint> &waypoints) const
{
    QByteArray payload;
    payload.reserve(static_cast<int>(waypoints.size()) * 64);
    for (const Waypoint &wp : waypoints) {
        if (wp.isMapperHome())
            continue;
        payload.append(QByteArray::number(wp.sequence()));
        payload.append('|');
        payload.append(wp.waypointType().toUtf8());
        payload.append('|');
        payload.append(QByteArray::number(wp.x(), 'f', 5));
        payload.append('|');
        payload.append(QByteArray::number(wp.y(), 'f', 5));
        payload.append('|');
        payload.append(QByteArray::number(wp.z(), 'f', 5));
        payload.append('|');
        payload.append(QByteArray::number(wp.yawAngle(), 'f', 3));
        payload.append('|');
        payload.append(QByteArray::number(wp.holdTime(), 'f', 3));
        payload.append('\n');
    }
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

void PathPlannerWidget::updateDirtyState()
{
    if (!m_hasUploadedSnapshot) {
        m_waypointsDirtySinceUpload = false;
        return;
    }

    m_waypointsDirtySinceUpload = (waypointFingerprint(m_openglWidget->waypoints()) != m_uploadedWaypointFingerprint);
}

PathPlannerWidget::MissionWorkspacePhase PathPlannerWidget::currentMissionPhase() const
{
    const bool hasWaypoints = m_openglWidget && !m_openglWidget->waypoints().empty();
    const bool uploaded = m_droneController && m_droneController->getCurrentMission().uploaded && !m_waypointsDirtySinceUpload;
    const bool running = m_droneController && m_droneController->isMissionRunning();
    const bool paused = running && m_droneController->isMissionPaused();

    if (paused)
        return MissionWorkspacePhase::Paused;
    if (running)
        return MissionWorkspacePhase::Running;
    if (uploaded)
        return MissionWorkspacePhase::UploadedReady;
    if (hasWaypoints)
        return MissionWorkspacePhase::ReadyToUpload;
    return MissionWorkspacePhase::EditingDraft;
}

void PathPlannerWidget::setEditingLocked(bool locked)
{
    if (!m_openglWidget || !m_waypointTable) {
        m_editingLocked = locked;
        return;
    }

    const bool lockChanged = (m_editingLocked != locked);
    m_editingLocked = locked;

    if (lockChanged) {
        const QSignalBlocker bc(m_createModeButton);
        const QSignalBlocker bt(m_transformModeButton);
        if (locked) {
            m_createModeButton->setChecked(false);
            m_transformModeButton->setChecked(false);
            m_openglWidget->setNavigationOnly(true);
        } else {
            m_transformModeButton->setChecked(true);
            m_createModeButton->setChecked(false);
            m_openglWidget->setNavigationOnly(false);
            m_openglWidget->setEditorTools(false, true);
        }
    }

    m_createModeButton->setEnabled(!locked);
    m_transformModeButton->setEnabled(!locked);
    m_undoEditButton->setEnabled(!locked && m_openglWidget->canUndo());
    m_redoEditButton->setEnabled(!locked && m_openglWidget->canRedo());
    m_waypointDefaultsButton->setEnabled(!locked);
    m_pathNameEdit->setEnabled(!locked);
    m_waypointTable->setEditTriggers(locked ? QAbstractItemView::NoEditTriggers
                                            : (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed));
}

void PathPlannerWidget::clearStaleMapperErrorBanner()
{
    if (m_lastControllerError.contains(QStringLiteral("VOXL Mapper"), Qt::CaseInsensitive)) {
        m_lastControllerError.clear();
        updateMissionChrome();
    }
}

void PathPlannerWidget::updateMissionChrome()
{
    const MissionWorkspacePhase phase = currentMissionPhase();
    const bool connected = m_droneController && m_droneController->isConnected();
    const bool hasWaypoints = m_openglWidget && !m_openglWidget->waypoints().empty();
    const bool uploaded = m_droneController && m_droneController->getCurrentMission().uploaded;

    if (!hasWaypoints && m_isPlayingPathPreview)
        stopPathPreviewAnimation();

    QString shortMode;
    switch (phase) {
    case MissionWorkspacePhase::EditingDraft:
        shortMode = "Editing";
        break;
    case MissionWorkspacePhase::ReadyToUpload:
        shortMode = "Ready to upload";
        break;
    case MissionWorkspacePhase::UploadedReady:
        shortMode = "Uploaded";
        break;
    case MissionWorkspacePhase::Running:
        shortMode = "Running";
        break;
    case MissionWorkspacePhase::Paused:
        shortMode = "Paused";
        break;
    }

    const QString vehicleText = connected
        ? QString("Connected • %1 • %2")
              .arg(m_droneController->getCurrentStatus().armed ? "Armed" : "Disarmed")
              .arg(m_droneController->getCurrentStatus().flightMode)
        : QStringLiteral("Offline");

    QString detail;
    if (m_waypointsDirtySinceUpload && uploaded) {
        detail = "Re-upload to enable Start";
    } else if (!m_lastControllerError.isEmpty()) {
        detail = m_lastControllerError;
    } else if (!connected && uploaded && !m_waypointsDirtySinceUpload && phase == MissionWorkspacePhase::UploadedReady) {
        detail = "Staged locally (offline test)";
    } else if (!connected && (phase == MissionWorkspacePhase::UploadedReady ||
                              phase == MissionWorkspacePhase::Running ||
                              phase == MissionWorkspacePhase::Paused)) {
        detail = "Connect to start";
    } else if (!m_lastMissionStatusText.isEmpty()) {
        detail = m_lastMissionStatusText;
    } else if (phase == MissionWorkspacePhase::EditingDraft) {
        detail = hasWaypoints ? "Toolbar ↑ uploads (works offline)" : "Add waypoints";
    } else if (phase == MissionWorkspacePhase::ReadyToUpload) {
        detail = "Tap ↑ to upload";
    } else if (phase == MissionWorkspacePhase::UploadedReady) {
        detail = "Ready to run";
    } else if (phase == MissionWorkspacePhase::Running) {
        detail = "Mission active";
    } else if (phase == MissionWorkspacePhase::Paused) {
        detail = "Mission paused";
    }

    QString html;
    if (!detail.isEmpty()) {
        html = QString("<span style=\"font-weight:600;color:#e5e7eb;\">%1</span>"
                       " <span style=\"color:#6b7280;\">—</span> "
                       "<span style=\"color:#fbbf24;\">%2</span>")
                   .arg(shortMode.toHtmlEscaped(), detail.toHtmlEscaped());
    } else {
        html = QString("<span style=\"font-weight:600;color:#e5e7eb;\">%1</span>")
                   .arg(shortMode.toHtmlEscaped());
    }

    if (m_missionStatusLabel) {
        m_missionStatusLabel->setText(html);
        m_missionStatusLabel->setStyleSheet("QLabel { color: #9ca3af; border: none; font-size: 11px; }");
        m_missionStatusLabel->setToolTip(connected ? vehicleText : QString());
    }

    const bool shouldLockEditing = (phase == MissionWorkspacePhase::UploadedReady ||
                                    phase == MissionWorkspacePhase::Running ||
                                    phase == MissionWorkspacePhase::Paused) &&
                                   !m_waypointsDirtySinceUpload;
    setEditingLocked(shouldLockEditing);

    const bool missionToolbarActive =
        m_droneController && m_droneController->getCurrentMission().uploaded && !m_waypointsDirtySinceUpload;

    if (missionToolbarActive && m_isPlayingPathPreview)
        stopPathPreviewAnimation();

    if (m_topBarPreUploadToolbar)
        m_topBarPreUploadToolbar->setVisible(!missionToolbarActive);
    if (m_topBarMissionCluster)
        m_topBarMissionCluster->setVisible(missionToolbarActive);

    if (m_playPathPreviewButton) {
        if (missionToolbarActive)
            m_playPathPreviewButton->setEnabled(false);
        else
            m_playPathPreviewButton->setEnabled(hasWaypoints && !m_isPlayingPathPreview);
    }

    const bool canUpload = hasWaypoints;
    const bool canStart = connected && uploaded && !m_waypointsDirtySinceUpload;
    const bool running = (phase == MissionWorkspacePhase::Running);
    const bool paused = (phase == MissionWorkspacePhase::Paused);

    m_uploadMissionButton->setEnabled(canUpload);
    if (m_missionPlayButton) {
        m_missionPlayButton->setText(QString::fromUtf8("\xE2\x96\xB6"));
        if (running) {
            m_missionPlayButton->setToolTip("Start Mission");
            m_missionPlayButton->setEnabled(false);
        } else if (paused) {
            m_missionPlayButton->setToolTip("Restart mission from the first waypoint");
            m_missionPlayButton->setEnabled(connected);
        } else {
            m_missionPlayButton->setToolTip("Start Mission");
            m_missionPlayButton->setEnabled(canStart);
        }
    }
    if (m_missionPauseContinueButton) {
        if (paused) {
            m_missionPauseContinueButton->setText(QString::fromUtf8("\xE2\x8F\xAF"));
            m_missionPauseContinueButton->setToolTip("Continue Mission (resume from here)");
            m_missionPauseContinueButton->setEnabled(connected);
        } else if (running) {
            m_missionPauseContinueButton->setText(QString::fromUtf8("\xE2\x96\xA0"));
            m_missionPauseContinueButton->setToolTip("Pause Mission (hover in place)");
            m_missionPauseContinueButton->setEnabled(connected);
        } else {
            m_missionPauseContinueButton->setText(QString::fromUtf8("\xE2\x96\xA0"));
            m_missionPauseContinueButton->setToolTip("Pause (available while mission is running)");
            m_missionPauseContinueButton->setEnabled(false);
        }
    }

    const bool missionFlightActive = running || paused;
    if (m_topBarLandButton)
        m_topBarLandButton->setEnabled(connected && missionFlightActive);
    if (m_topBarRtlButton)
        m_topBarRtlButton->setEnabled(connected && missionFlightActive);
    if (m_topBarForceDisarmButton)
        m_topBarForceDisarmButton->setEnabled(connected && missionFlightActive);
    if (m_topBarFlightTermButton)
        m_topBarFlightTermButton->setEnabled(connected && missionFlightActive);

}

bool PathPlannerWidget::saveToJson(const QString &path, const QString &mapperMapBundleFolderName)
{
    QJsonObject root;
    root["version"] = 1;

    const QFileInfo outInfo(path);
    root["name"] = FlightPath::displayNameFromFileBase(outInfo.baseName());
    if (!m_roomId.trimmed().isEmpty())
        root["room_id"] = m_roomId.trimmed();
    if (!m_roomName.trimmed().isEmpty())
        root["room_name"] = m_roomName.trimmed();
    const QDateTime now = QDateTime::currentDateTime();
    root["created_at"] = now.toString(Qt::ISODate);
    root["modified_at"] = now.toString(Qt::ISODate);
    if (!m_mapperMapPath.trimmed().isEmpty())
        root["mapper_map_path"] = m_mapperMapPath.trimmed();
    if (!mapperMapBundleFolderName.trimmed().isEmpty())
        root["mapper_map_bundle"] = mapperMapBundleFolderName.trimmed();

    if (m_openglWidget) {
        const bool hasRenderSnapshot = !m_openglWidget->mapperRenderPositionsLogical().isEmpty()
                                       || !m_openglWidget->mapperMeshPositionsLogical().isEmpty();
        if (hasRenderSnapshot) {
            QJsonObject snapshot;
            snapshot["format"] = QStringLiteral("planner-logical-v1");

            if (!m_openglWidget->mapperRenderPositionsLogical().isEmpty()) {
                QJsonObject pathRender;
                pathRender["positions"] = vector3DListToJson(m_openglWidget->mapperRenderPositionsLogical());
                pathRender["colors"] = colorListToJson(m_openglWidget->mapperRenderColors());
                snapshot["path_render"] = pathRender;
            }

            if (!m_openglWidget->mapperMeshPositionsLogical().isEmpty()) {
                QJsonObject mesh;
                mesh["positions"] = vector3DListToJson(m_openglWidget->mapperMeshPositionsLogical());
                mesh["colors"] = colorListToJson(m_openglWidget->mapperMeshColors());
                mesh["triangle_indices"] = indexListToJson(m_openglWidget->mapperMeshTriangleIndices());
                snapshot["mesh"] = mesh;
            }

            root["mapper_render_snapshot"] = snapshot;
        }
    }

    QJsonArray waypointsArray;
    const auto &waypoints = m_openglWidget->waypoints();
    for (const auto &wp : waypoints)
    {
        if (!wp.isMapperHome())
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
    m_mapperMapPath = root["mapper_map_path"].toString();
    m_mapperMapBundleDir.clear();
    const QString bundleName = root["mapper_map_bundle"].toString();
    if (!bundleName.isEmpty()) {
        const QString abs = QFileInfo(path).absoluteDir().filePath(bundleName);
        if (QDir(abs).exists())
            m_mapperMapBundleDir = QDir::toNativeSeparators(abs);
    }
    if (m_mapperMapBundleDir.isEmpty() && !m_roomMapDir.trimmed().isEmpty()) {
        const QString roomBundle = QDir(m_roomMapDir).filePath(QStringLiteral("mapper_map"));
        if (QDir(roomBundle).exists())
            m_mapperMapBundleDir = QDir::toNativeSeparators(roomBundle);
    }
    if (m_mapperMapPath.trimmed().isEmpty() && !m_roomMapDir.trimmed().isEmpty()) {
        QFile metaFile(QDir(m_roomMapDir).filePath(QStringLiteral("map.json")));
        if (metaFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument metaDoc = QJsonDocument::fromJson(metaFile.readAll());
            if (metaDoc.isObject())
                m_mapperMapPath = metaDoc.object().value(QStringLiteral("remote_path")).toString();
        }
    }
    std::vector<Waypoint> waypoints;

    for (const QJsonValue &value : waypointsArray)
    {
        if (value.isObject())
        {
            Waypoint w = Waypoint::fromJson(value.toObject());
            if (!w.isMapperHome())
                waypoints.push_back(w);
        }
    }

    m_openglWidget->setWaypoints(waypoints);

    QVector<QVector3D> renderPositions;
    QVector<QColor> renderColors;
    QVector<QVector3D> meshPositions;
    QVector<QColor> meshColors;
    QVector<quint32> meshTriangleIndices;
    const QJsonObject snapshot = root.value(QStringLiteral("mapper_render_snapshot")).toObject();
    if (!snapshot.isEmpty()) {
        const QJsonObject pathRender = snapshot.value(QStringLiteral("path_render")).toObject();
        renderPositions = vector3DListFromJson(pathRender.value(QStringLiteral("positions")).toArray());
        renderColors = colorListFromJson(pathRender.value(QStringLiteral("colors")).toArray());

        const QJsonObject mesh = snapshot.value(QStringLiteral("mesh")).toObject();
        meshPositions = vector3DListFromJson(mesh.value(QStringLiteral("positions")).toArray());
        meshColors = colorListFromJson(mesh.value(QStringLiteral("colors")).toArray());
        meshTriangleIndices = indexListFromJson(mesh.value(QStringLiteral("triangle_indices")).toArray());
    }
    m_openglWidget->setMapperRenderData(renderPositions, renderColors);
    m_openglWidget->setMapperMeshData(meshPositions, meshColors, meshTriangleIndices);

    updateWaypointTable();
    emitWaypointsChanged();

    if (!waypoints.empty())
        onWaypointSelected(waypoints.front().sequence());

    return true;
}


void PathPlannerWidget::setPlannerPathsDirectory(const QString &dir)
{
    m_plannerPathsDir = dir;
}

void PathPlannerWidget::setPlannerRoomContext(const QString &roomId, const QString &roomName,
                                              const QString &pathsDir, const QString &mapDir)
{
    m_roomId = roomId;
    m_roomName = roomName;
    m_roomMapDir = mapDir;
    setPlannerPathsDirectory(pathsDir);
}

void PathPlannerWidget::setDroneController(DroneController *controller)
{
    m_droneController = controller;

    if (m_droneController) {
        connect(m_droneController, &DroneController::connectionStatusChanged,
                this, [this](bool connected) {
                    if (!connected)
                        removeMapperHomeWaypointAndRefresh();
                    if (connected)
                        clearStaleMapperErrorBanner();
                    updateMissionChrome();
                });
        connect(m_droneController, &DroneController::statusUpdated,
                this, [this](const DroneStatus &) {
                    updateMissionChrome();
                });
        connect(m_droneController, &DroneController::mapperPoseUpdated,
                this, [this](const QVector3D &positionLogical, float yawDeg) {
                    clearStaleMapperErrorBanner();
                    if (m_openglWidget)
                        m_openglWidget->setDronePoseLogical(positionLogical, yawDeg);
                });
        connect(m_droneController, &DroneController::mapperRenderUpdated,
                this, [this](const QVector<QVector3D> &positionsLogical, const QVector<QColor> &colors) {
                    if (!positionsLogical.isEmpty())
                        clearStaleMapperErrorBanner();
                    if (m_openglWidget)
                        m_openglWidget->setMapperRenderData(positionsLogical, colors);
                });
        connect(m_droneController, &DroneController::mapperMeshUpdated,
                this, [this](const QVector<QVector3D> &positionsLogical, const QVector<QColor> &colors, const QVector<quint32> &triangleIndices) {
                    if (!positionsLogical.isEmpty() || !triangleIndices.isEmpty())
                        clearStaleMapperErrorBanner();
                    if (m_openglWidget)
                        m_openglWidget->setMapperMeshData(positionsLogical, colors, triangleIndices);
                });
        connect(m_droneController, &DroneController::missionStatusChanged,
                this, [this](const QString &status) {
                    m_lastMissionStatusText = status;
                    updateMissionChrome();
                });
        connect(m_droneController, &DroneController::errorOccurred,
                this, [this](const QString &error) {
                    m_lastControllerError = error;
                    updateMissionChrome();
                });
        connect(m_droneController, &DroneController::mapperBundleDownloadFinished,
                this, &PathPlannerWidget::onMapperBundleDownloadFinished, Qt::UniqueConnection);
    }
    updateMissionChrome();
}

void PathPlannerWidget::onMapperBundleDownloadFinished(bool success, const QString &message)
{
    if (m_backgroundBundleJsonPath.isEmpty())
        return;

    const QString jsonPath = m_backgroundBundleJsonPath;
    const QString bundleFolderName = m_backgroundBundleFolderName;
    m_backgroundBundleJsonPath.clear();
    m_backgroundBundleFolderName.clear();

    if (m_missionStatusLabel) {
        m_missionStatusLabel->clear();
        updateMissionChrome();
    }

    const QString bundleForJson = success ? bundleFolderName : QString();
    if (!saveToJson(jsonPath, bundleForJson)) {
        QMessageBox::warning(this, "Mapper map copy",
                             QStringLiteral("Could not update the saved JSON after copying the map."));
        return;
    }

    const QString displayName = FlightPath::displayNameFromFileBase(QFileInfo(jsonPath).baseName());

    m_mapperMapBundleDir.clear();
    if (success) {
        const QString abs = QFileInfo(jsonPath).absoluteDir().filePath(bundleFolderName);
        if (QDir(abs).exists())
            m_mapperMapBundleDir = QDir::toNativeSeparators(abs);

        if (!m_roomMapDir.trimmed().isEmpty()) {
            QDir().mkpath(m_roomMapDir);
            QJsonObject mapMeta;
            mapMeta[QStringLiteral("display_name")] = m_roomName.trimmed().isEmpty()
                                                          ? displayName
                                                          : m_roomName.trimmed();
            mapMeta[QStringLiteral("remote_path")] = m_mapperMapPath.trimmed();
            mapMeta[QStringLiteral("bundle_dir")] = QStringLiteral("mapper_map");
            mapMeta[QStringLiteral("mesh_path")] = QStringLiteral("map.ply");
            mapMeta[QStringLiteral("updated_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);
            QFile metaFile(QDir(m_roomMapDir).filePath(QStringLiteral("map.json")));
            if (metaFile.open(QIODevice::WriteOnly | QIODevice::Text))
                metaFile.write(QJsonDocument(mapMeta).toJson(QJsonDocument::Indented));
        }
    }

    if (success) {
        QMessageBox::information(
            this,
            "Mapper map copy",
            QString("Mapper map for \"%1\" is on disk next to the JSON.\n\nJSON:\n%2\n\nLocal folder:\n%3\n\nVOXL load "
                    "path:\n%4")
                .arg(displayName, jsonPath, m_mapperMapBundleDir, m_mapperMapPath));
    } else {
        QMessageBox::warning(
            this,
            "Mapper map copy",
            QString("Waypoint file was saved earlier, but copying the map from VOXL failed "
                    "(scp/SSH, firewall, or the map had not finished writing on the drone).\n\n%1\n\nJSON:\n%2\n\n"
                    "VOXL load path:\n%3")
                .arg(message, jsonPath, m_mapperMapPath));
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
    
    // Upload via DroneController
    m_lastControllerError.clear();
    if (m_missionStatusLabel) {
        m_missionStatusLabel->setText(
            "<span style=\"color:#fbbf24;font-weight:600;\">Uploading mission…</span>");
        m_missionStatusLabel->setStyleSheet("QLabel { border: none; font-size: 11px; }");
        m_missionStatusLabel->setToolTip(QString());
    }
    
    if (m_droneController->isConnected())
        m_droneController->uploadMapperMission(waypoints);
    else
        m_droneController->stageMapperMissionLocally(waypoints);

    if (m_droneController->getCurrentMission().uploaded) {
        m_hasUploadedSnapshot = true;
        m_waypointsDirtySinceUpload = false;
        m_uploadedWaypointFingerprint = waypointFingerprint(waypoints);
        m_lastMissionStatusText = m_droneController->isConnected()
            ? "Mapper mission staged on VOXL"
            : "Mapper mission staged locally (offline test)";
        m_lastControllerError.clear();
        if (m_droneController->isConnected()) {
            QMessageBox::information(this, "Mission Upload",
                QString("Mission uploaded successfully.\n\nWaypoints: %1\nTotal Distance: %2 m")
                    .arg(waypoints.size())
                    .arg(missionPath.totalDistance(), 0, 'f', 2));
        } else {
            QMessageBox::information(this, "Mission Upload (offline test)",
                QString("Mission staged locally (not sent to a drone).\n\nWaypoints: %1\nTotal Distance: %2 m")
                    .arg(waypoints.size())
                    .arg(missionPath.totalDistance(), 0, 'f', 2));
        }
    } else if (m_lastControllerError.isEmpty()) {
        m_lastControllerError = "Mission upload failed.";
    }
    
    // Clean up temp file
    QFile::remove(missionFilePath);
    updateMissionChrome();
}

void PathPlannerWidget::onMissionPlayClicked()
{
    if (!m_droneController || !m_droneController->isConnected())
        return;

    const MissionWorkspacePhase phase = currentMissionPhase();
    if (phase == MissionWorkspacePhase::Paused) {
        if (QMessageBox::question(
                this,
                "Restart Mission",
                "Restart the mission from the first waypoint?\n\n"
                "Use Continue to resume from the current paused position.",
                QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        m_lastControllerError.clear();
        m_droneController->startMission();
        m_lastMissionStatusText = "Mission restarted from start";
        updateMissionChrome();
        return;
    }

    onRunMission();
}

void PathPlannerWidget::onMissionPauseContinueClicked()
{
    if (!m_droneController || !m_droneController->isConnected())
        return;

    if (m_droneController->isMissionRunning() && m_droneController->isMissionPaused()) {
        onResumeMission();
        return;
    }
    if (m_droneController->isMissionRunning() && !m_droneController->isMissionPaused()) {
        onPauseMission();
    }
}

void PathPlannerWidget::onRunMission()
{
    if (!m_droneController) {
        return;
    }
    if (!m_droneController->isConnected()) {
        QMessageBox::warning(this, "Run Mission", "Drone is disconnected.");
        m_lastControllerError = "Start unavailable: drone disconnected";
        updateMissionChrome();
        return;
    }
    if (!m_droneController->getCurrentMission().uploaded || m_waypointsDirtySinceUpload) {
        QMessageBox::warning(this, "Run Mission", "Upload the latest mission before starting.");
        updateMissionChrome();
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Run Mission", 
        "Are you sure you want to start the mapper mission?\n\n"
        "The drone must already be airborne in the correct offboard/trajectory mode. "
        "The app will chain VOXL Mapper plan_to/follow_path commands.",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        m_lastControllerError.clear();
        m_droneController->startMission();
        m_lastMissionStatusText = "Mission started";
        updateMissionChrome();
    }
}

void PathPlannerWidget::onPauseMission()
{
    if (!m_droneController || !m_droneController->isConnected())
        return;

    m_droneController->pauseMission();
    m_lastControllerError.clear();
    m_lastMissionStatusText = "Mission paused";
    updateMissionChrome();
}

void PathPlannerWidget::onResumeMission()
{
    if (!m_droneController || !m_droneController->isConnected())
        return;

    m_droneController->resumeMission();
    m_lastControllerError.clear();
    m_lastMissionStatusText = "Mission resumed";
    updateMissionChrome();
}

void PathPlannerWidget::onLandMission()
{
    if (!m_droneController || !m_droneController->isConnected())
        return;

    if (QMessageBox::question(this, "Land", "Command the drone to land now?") != QMessageBox::Yes)
        return;

    m_droneController->land();
    m_lastControllerError.clear();
    m_lastMissionStatusText = "Land command sent";
    updateMissionChrome();
}

void PathPlannerWidget::onReturnToLaunchMission()
{
    if (!m_droneController || !m_droneController->isConnected())
        return;

    if (QMessageBox::question(this, "Return to Launch", "Command return-to-launch now?") != QMessageBox::Yes)
        return;

    m_droneController->returnToLaunch();
    m_lastControllerError.clear();
    m_lastMissionStatusText = "Return-to-launch command sent";
    updateMissionChrome();
}

void PathPlannerWidget::onForceDisarmMission()
{
    if (!m_droneController || !m_droneController->isConnected())
        return;

    if (QMessageBox::warning(this, QStringLiteral("Force disarm"),
                             QStringLiteral("Send force-disarm? Motors stop immediately (software only)."),
                             QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel)
        != QMessageBox::Yes)
        return;

    m_droneController->forceDisarm();
    m_lastControllerError.clear();
    m_lastMissionStatusText = QStringLiteral("Force disarm requested");
    updateMissionChrome();
}

void PathPlannerWidget::onFlightTerminationMission()
{
    if (!m_droneController || !m_droneController->isConnected())
        return;

    if (QMessageBox::critical(this, QStringLiteral("Flight termination"),
                              QStringLiteral("Send PX4 flight termination?\n\n"
                                             "Strongest software stop if enabled (see CBRK_FLIGHTTERM). "
                                             "May require power-cycle. Not a hardware estop."),
                              QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel)
        != QMessageBox::Yes)
        return;

    m_droneController->flightTermination();
    m_lastControllerError.clear();
    m_lastMissionStatusText = QStringLiteral("Flight termination sent");
    updateMissionChrome();
}

void PathPlannerWidget::onReturnToEdit()
{
    if (!m_hasUploadedSnapshot || m_waypointsDirtySinceUpload) {
        setEditingLocked(false);
        updateMissionChrome();
        return;
    }

    const QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Return to Edit",
        "Unlock editing for this mission?\n\n"
        "Changes will mark the mission as not uploaded until you upload again.",
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    setEditingLocked(false);
    m_waypointsDirtySinceUpload = true;
    updateMissionChrome();
}
