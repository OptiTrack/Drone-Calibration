#ifndef PATHPLANNERWIDGET_H
#define PATHPLANNERWIDGET_H

#include <QWidget>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPaintEvent>
#include <QModelIndex>
#include <QSet>
#include <QVector>
#include <QEvent>
#include <vector>
#include "../models/waypoint.h"

class DroneController;

class PathPlannerOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum ViewMode {
        TopDownMode,    ///< Orthographic top-down view for planning
        View3DMode      ///< Free 3D inspection mode
    };

    enum InteractionMode {
        NavigateMode,   ///< Camera navigation only
        CreateMode,     ///< Left-click creates waypoints
        SelectMode,     ///< Left-click selects waypoints
        TransformMode   ///< Left-drag gizmo handles to move selected waypoint
    };

    explicit PathPlannerOpenGLWidget(QWidget *parent = nullptr);
    ~PathPlannerOpenGLWidget();

    // Waypoint management
    void setWaypoints(const std::vector<Waypoint> &waypoints);
    void addWaypoint(const QVector3D &point);
    void updateWaypoint(int id, const Waypoint &wp);
    void removeWaypoint(int id);
    void clearWaypoints();
    void setSelectedWaypoint(int id);
    const std::vector<Waypoint>& waypoints() const { return m_waypoints; }
    
    // View mode
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_viewMode; }
    
    // Camera control
    void resetCamera();
    void setDefaultAltitude(float altitude) { m_defaultAltitude = altitude; }
    float defaultAltitude() const { return m_defaultAltitude; }
    void setDefaultAcceptanceRadius(float radius) { m_defaultAcceptanceRadius = radius; }
    float defaultAcceptanceRadius() const { return m_defaultAcceptanceRadius; }
    void setDefaultHoldTime(float timeSec) { m_defaultHoldTime = timeSec; }
    float defaultHoldTime() const { return m_defaultHoldTime; }
    void setDefaultYawAngle(float yawDeg) { m_defaultYawAngle = yawDeg; }
    float defaultYawAngle() const { return m_defaultYawAngle; }
    void setInteractionMode(InteractionMode mode);
    InteractionMode interactionMode() const { return m_interactionMode; }
    bool undo();
    bool redo();
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    bool duplicateSelectedWaypoint();
    bool deleteSelectedWaypoint();

signals:
    void waypointSelected(int id);
    void waypointAdded(const Waypoint &waypoint);
    void waypointMoved(int id, const QVector3D &newPosition);
    void selectionChanged(const QSet<int> &selectedIds);
    void transformStarted(int id);
    void transformUpdated(int id);
    void transformEnded(int id);
    void editHistoryStateChanged(bool canUndo, bool canRedo);
    void waypointsEdited();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum class TransformHandle
    {
        None,
        AxisX,
        AxisY,
        AxisZ,
        PlaneXY,
        PlaneXZ,
        PlaneYZ,
        Yaw
    };

    struct EditCommand
    {
        std::vector<Waypoint> before;
        std::vector<Waypoint> after;
    };

    void setupShaders();
    void setupBuffers();
    void drawGrid();
    void drawWaypoints();
    void drawPath();
    void drawAxes();
    void drawGizmo();
    void drawWaypointLabels(QPainter &painter);
    void updateCamera();
    void updateProjection();
    QVector3D screenToWorld(const QPoint &screenPos, float depth = 0.0f);
    QVector3D screenToWorldOnYPlane(const QPoint &screenPos, float worldY) const;
    QPoint worldToScreen(const QVector3D &worldPos) const;
    /// Screen position and NDC depth (for label ordering / occlusion hints). Returns false if behind camera.
    bool projectWorldToScreenDepth(const QVector3D &worldPos, QPoint *outPx, float *outNdcZ) const;
    /// Approximate projected radius in pixels (for HUD rings that track 3D sphere size).
    float screenSpaceSphereRadiusPx(const QVector3D &worldCenter, float worldRadius) const;
    /// Same geometry as screenSpaceSphereRadiusPx before min/max clamp (for cone vs HUD behavior).
    float projectedSphereRadiusPxRaw(const QVector3D &worldCenter, float worldRadius) const;
    /// Length scale, extra base radius scale, and ring position (0..1 along forward) for the waypoint yaw cone.
    void waypointMarkerConeParameters(const QVector3D &waypointCenterWorld, float &outLengthScale,
                                      float &outBaseRadiusScale, float &outRingAlongF) const;
    int findWaypointAt(const QPoint &screenPos);
    int findWaypointAt(const QPoint &screenPos, float *outDistancePx, float *outDepthNdc) const;
    int findSegmentAt(const QPoint &screenPos, QVector3D *insertWorldPoint = nullptr, float maxDistancePx = 12.0f) const;
    TransformHandle findTransformHandleAt(const QPoint &screenPos) const;
    bool updateWaypointLogicalPosition(int id, const QVector3D &newLogicalPosition);
    bool updateWaypointYawAngle(int id, float yawDeg);
    QVector3D selectedWaypointLogicalPosition() const;
    float selectedWaypointYawDeg() const;
    QVector3D gizmoAxisDirectionWorld(TransformHandle handle) const;
    QVector3D gizmoCenterWorld() const;
    /// Screen-space linearization: how many world meters along `axisWorldUnit` correspond to `screenDelta` pixels (origin fixed for whole drag).
    float worldDeltaAlongAxisFromScreenDelta(const QVector3D &originWorld, const QVector3D &axisWorldUnit,
                                             const QPoint &screenDelta) const;
    /// Same for a plane spanned by two orthogonal world unit vectors; outputs world deltas along u0 and u1.
    bool worldDeltasOnPlaneFromScreenDelta(const QVector3D &originWorld, const QVector3D &u0World,
                                           const QVector3D &u1World, const QPoint &screenDelta,
                                           float &outDu0, float &outDu1) const;
    void appendLine(const QVector3D &a, const QVector3D &b, const QVector3D &color,
                    QVector<float> &vertices, QVector<float> &colors) const;
    void appendSphere(const QVector3D &center, float radius, const QVector3D &color,
                      QVector<float> &vertices, QVector<float> &colors) const;
    void commitEdit(const std::vector<Waypoint> &before, const std::vector<Waypoint> &after);
    void clearRedoHistory();
    void updateHistorySignals();
    
    // OpenGL resources
    QOpenGLShaderProgram *m_shaderProgram;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLBuffer m_indexBuffer;
    QOpenGLVertexArrayObject m_vao;
    
    // Camera
    QMatrix4x4 m_projectionMatrix;
    QMatrix4x4 m_viewMatrix;
    QMatrix4x4 m_modelMatrix;
    QVector3D m_cameraPosition;
    QVector3D m_cameraTarget;
    QVector3D m_cameraUp;
    float m_cameraDistance;
    float m_cameraYaw;
    float m_cameraPitch;
    
    // View mode
    ViewMode m_viewMode;
    InteractionMode m_interactionMode;
    float m_orthoZoom;
    float m_defaultAltitude;
    float m_defaultAcceptanceRadius;
    float m_defaultHoldTime;
    float m_defaultYawAngle;
    
    // Waypoints
    std::vector<Waypoint> m_waypoints;
    int m_selectedWaypoint;  // ID of selected waypoint (-1 if none)
    QSet<int> m_selectedWaypointIds;
    int m_hoveredWaypoint;
    int m_hoveredSegment;
    
    // Interaction
    QPoint m_lastMousePos;
    bool m_mousePressed;
    bool m_isDragging;
    bool m_draggingTransform;
    TransformHandle m_activeHandle;
    QPoint m_dragStartMousePos;
    QVector3D m_dragStartLogicalPos;
    QVector3D m_dragGizmoOriginWorld;
    float m_dragYawPlaneAngleStartRad;
    float m_dragStartYawDeg;
    std::vector<Waypoint> m_dragBeforeSnapshot;
    QVector3D m_lastHoverPreviewWorldPos;
    bool m_hasHoverPreview;
    bool m_pendingCreatePlacement;
    bool m_createPressOnExistingWaypoint;

    QVector<EditCommand> m_undoStack;
    QVector<EditCommand> m_redoStack;
    bool m_applyingHistory;
    
    // Animation
    QTimer *m_animationTimer;
    float m_animationTime;
};

class PathPlannerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PathPlannerWidget(QWidget *parent = nullptr);
    ~PathPlannerWidget();

    // Drone controller
    void setDroneController(DroneController *controller);

    // Waypoint management
    void addWaypoint(const QVector3D &pos);
    void updateWaypoint(int id, const Waypoint &wp);
    void removeWaypoint(int id);
    const std::vector<Waypoint>& waypoints() const;
    
    // Legacy support
    void loadPoints(const QVector<QVector3D> &points);
    void clearPath();
    
    // JSON persistence
    bool saveToJson(const QString &path);
    bool loadFromJson(const QString &path);

signals:
    void pathSaved(const QString &name, const QVector<QVector3D> &points);
    void waypointsChanged(const std::vector<Waypoint> &waypoints);

private slots:
    void onClearPath();
    void onSavePath();
    void onLoadPath();
    void onUploadMission();
    void onRunMission();
    void onCancelMission();
    void onWaypointSelected(int id);
    void onWaypointCellChanged(int row, int column);
    void onPlayPath();
    void onStopPath();
    void onPathAnimationTimer();
    void onViewModeChanged();
    void onInteractionModeChanged(int index);
    void onWaypointRowsMoved(const QModelIndex &parent, int start, int end,
                             const QModelIndex &destination, int row);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void setupUI();
    void setupTopBar();
    void setupControls();
    void setupWaypointTable();
    void updateViewTogglePlacement();
    void updateWaypointTable();
    void startPathAnimation();
    void stopPathAnimation();
    void emitWaypointsChanged();
    void updateUndoRedoButtons();
    
    // Main layouts
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_contentLayout;
    QWidget *m_topBarWidget;
    QVBoxLayout *m_controlsLayout;
    
    // 3D View
    PathPlannerOpenGLWidget *m_openglWidget;
    
    // Control panels
    QGroupBox *m_waypointGroup;
    QGroupBox *m_viewGroup;
    
    // Waypoint controls
    QTableWidget *m_waypointTable;
    QLabel *m_waypointCountLabel;
    QToolButton *m_waypointDefaultsButton;
    QDoubleSpinBox *m_defaultAcceptanceRadiusSpinBox;
    QDoubleSpinBox *m_defaultHoldSpinBox;
    QDoubleSpinBox *m_defaultYawSpinBox;
    
    // Path controls
    QToolButton *m_pathMenuButton;
    QPushButton *m_uploadMissionButton;
    QPushButton *m_runMissionButton;
    QPushButton *m_cancelMissionButton;
    QPushButton *m_createModeButton;
    QPushButton *m_transformModeButton;
    QPushButton *m_playPathButton;
    QPushButton *m_stopPathButton;
    QLineEdit *m_pathNameEdit;
    QLabel *m_missionStatusLabel;
    
    // View controls
    QPushButton *m_resetCameraButton;
    QPushButton *m_viewModeButton;
    QDoubleSpinBox *m_defaultAltitudeSpinBox;
    QPushButton *m_undoEditButton;
    QPushButton *m_redoEditButton;
    
    // Animation
    QTimer *m_pathAnimationTimer;
    int m_currentAnimationWaypoint;
    float m_animationProgress;
    bool m_isPlayingPath;
    
    // Current waypoint selection
    int m_selectedWaypoint;  // ID of selected waypoint (-1 if none)
    
    // Drone connection
    DroneController *m_droneController;

    bool m_updatingWaypointTable;
    bool m_reorderingWaypointRows;
};

#endif // PATHPLANNERWIDGET_H
