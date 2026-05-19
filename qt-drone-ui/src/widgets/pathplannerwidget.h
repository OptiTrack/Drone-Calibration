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
#include <QColor>
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
#include <QAction>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPaintEvent>
#include <QModelIndex>
#include <QSet>
#include <QVector>
#include <QEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QUrl>
#include <vector>
#include "../models/waypoint.h"
#include "../models/trajectory.h"

class DroneController;
class QFrame;

class PathPlannerOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum ViewMode {
        TopDownMode,    ///< Orthographic top-down view for planning
        View3DMode      ///< Free 3D inspection mode
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
    void setNavigationOnly(bool on);
    void setDecorationsHidden(bool hidden);
    bool decorationsHidden() const { return m_decorationsHidden; }
    void setEditorTools(bool createEnabled, bool transformEnabled);
    /// One-shot bias for the next addWaypoint(): make it a curve point with the given radius.
    /// Sticks across multiple additions until cleared (called repeatedly from the UI on every toggle).
    void setNextWaypointAsCurve(bool enable, float defaultRadiusM);
    bool selectedWaypointIsCurve() const;
    bool createToolEnabled() const { return m_createToolEnabled; }
    bool transformToolEnabled() const { return m_transformToolEnabled; }
    bool undo();
    bool redo();
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    bool duplicateSelectedWaypoint();
    bool deleteSelectedWaypoint();
    void setDronePoseLogical(const QVector3D &positionLogical, float yawDeg);
    void setMapperRenderData(const QVector<QVector3D> &positionsLogical, const QVector<QColor> &colors);
    void setMapperMeshData(const QVector<QVector3D> &positionsLogical, const QVector<QColor> &colors, const QVector<quint32> &triangleIndices);
    /// Set / clear the low-opacity dashed "first pass" overlay that shows the raw recording
    /// behind a fitted curve path. Hidden during camera manipulation.
    void setRecordingBackbone(const QVector<QVector3D> &positionsLogical);
    void clearRecordingBackbone();
    const QVector<QVector3D> &mapperRenderPositionsLogical() const { return m_mapperRenderPositionsLogical; }
    const QVector<QColor> &mapperRenderColors() const { return m_mapperRenderColors; }
    const QVector<QVector3D> &mapperMeshPositionsLogical() const { return m_mapperMeshPositionsLogical; }
    const QVector<QColor> &mapperMeshColors() const { return m_mapperMeshColors; }
    const QVector<quint32> &mapperMeshTriangleIndices() const { return m_mapperMeshTriangleIndices; }

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
    void drawDroneAxes();
    void drawMapperRenderData();
    void drawRecordingBackbone();
    void drawGizmo();
    void drawWaypointLabels(QPainter &painter);
    // Trajectory visualization cache (rebuilt whenever waypoints or settings change).
    void rebuildVisualizationCacheIfNeeded();
    void setTrajectoryVisualization(bool on, float cruiseMs, float sampleHz);
    void invalidateTrajectoryCache();
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
    bool effectiveCreate() const;
    bool effectiveTransform() const;
    void commitEdit(const std::vector<Waypoint> &before, const std::vector<Waypoint> &after);
    void clearRedoHistory();
    void updateHistorySignals();
    void normalizeMapperHomeAndRenumberSequences();
    bool selectedWaypointIsMapperHome() const;
    
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
    bool m_navigationOnly = false;
    bool m_decorationsHidden = false;
    bool m_createToolEnabled = false;
    bool m_transformToolEnabled = true;
    bool  m_nextWaypointIsCurve = false;
    float m_nextWaypointRadiusM = 0.0f;
    float m_orthoZoom;
    float m_defaultAltitude;
    float m_defaultAcceptanceRadius;
    float m_defaultHoldTime;
    float m_defaultYawAngle;
    QVector3D m_dronePositionLogical;
    float m_droneYawDeg;
    bool m_hasDronePose;
    QVector<QVector3D> m_mapperRenderPositionsLogical;
    QVector<QColor> m_mapperRenderColors;
    // Dashed semi-transparent "first pass" overlay (raw flight-log positions, logical Z-up).
    QVector<QVector3D> m_recordingBackbonePositionsLogical;
    bool m_cameraInteracting = false;
    QTimer *m_backboneRevealTimer = nullptr;
    QVector<QVector3D> m_mapperMeshPositionsLogical;
    QVector<QColor> m_mapperMeshColors;
    QVector<quint32> m_mapperMeshTriangleIndices;
    
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

    // Trajectory visualization cache
    QVector<QVector3D> m_visualizationPath;    // logical-frame positions (Z-up)
    QVector<float>     m_visualizationSpeeds;  // |v| per sample, same length as positions
    bool m_visualizationDirty = true;
    bool m_trajectoryVisualizationMode = false;
    float m_visualizationCruiseMs = 2.5f;
    float m_visualizationSampleHz = 20.0f;

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

    /// Override the directory used for saving/loading path JSON files.
    /// Pass an empty string to fall back to the built-in plannerPathsDirectory().
    void setPlannerPathsDirectory(const QString &dir);
    void setPlannerRoomContext(const QString &roomId, const QString &roomName,
                               const QString &pathsDir, const QString &mapDir);

    void setNextWaypointAsCurve(bool enable, float defaultRadiusM);
    bool selectedWaypointIsCurve() const;

    // Waypoint management
    void addWaypoint(const QVector3D &pos);
    void updateWaypoint(int id, const Waypoint &wp);
    void removeWaypoint(int id);
    const std::vector<Waypoint>& waypoints() const;
    
    // Legacy support
    void loadPoints(const QVector<QVector3D> &points);
    void clearPath();
    
    // JSON persistence
    bool saveToJson(const QString &path, const QString &mapperMapBundleFolderName = QString());
    bool loadFromJson(const QString &path);
    /// Load waypoints + mapper metadata from disk; clears local mesh/plan preview; optional map reload uses clear-then-load on VOXL.
    bool loadPathFromFile(const QString &fileName, bool showSuccessDialog = true);
    /// Parse a FlightLogger recording, fit a curve-point path, drop a low-opacity backbone overlay
    /// of the raw trace into the scene. Returns false on read/parse failure.
    bool loadPathFromRecording(const QString &fileName);
    void clearMapperVisualization();

signals:
    void pathSaved(const QString &name, const QVector<QVector3D> &points);
    void waypointsChanged(const std::vector<Waypoint> &waypoints);

private slots:
    void onClearPath();
    void onSavePath();
    void onLoadPath();
    void onLoadPathFromRecording();
    void onMapperBundleDownloadFinished(bool success, const QString &message);
    void onUploadMission();
    void onMissionPlayClicked();
    void onMissionPauseContinueClicked();
    void onRunMission();
    void onPauseMission();
    void onResumeMission();
    void onLandMission();
    void onReturnToLaunchMission();
    void onForceDisarmMission();
    void onFlightTerminationMission();
    void onReturnToEdit();
    void onWaypointSelected(int id);
    void onWaypointCellChanged(int row, int column);
    void onPlayPathPreview();
    void onStopPathPreview();
    void onPathPreviewAnimationTimer();
    void onViewModeChanged();
    void applyEditorToolsFromButtons();
    void onWaypointRowsMoved(const QModelIndex &parent, int start, int end,
                             const QModelIndex &destination, int row);

private:
    enum class MissionWorkspacePhase {
        EditingDraft,
        ReadyToUpload,
        UploadedReady,
        Running,
        Paused
    };

    bool eventFilter(QObject *watched, QEvent *event) override;
    void setupUI();
    void setupTopBar();
    void setupControls();
    void setupWaypointTable();
    void updateViewTogglePlacement();
    void updateWaypointTable();
    void startPathPreviewAnimation();
    void stopPathPreviewAnimation();
    void emitWaypointsChanged();
    void updateUndoRedoButtons();
    void updateDirtyState();
    QString waypointFingerprint(const std::vector<Waypoint> &waypoints) const;
    MissionWorkspacePhase currentMissionPhase() const;
    void updateMissionChrome();
    void clearStaleMapperErrorBanner();
    void setEditingLocked(bool locked);
    
    // Main layouts
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_contentLayout;
    QWidget *m_topBarWidget;
    QVBoxLayout *m_controlsLayout;
    QWidget *m_topBarPreUploadToolbar;
    QWidget *m_topBarEditingCluster;
    QWidget *m_topBarMissionCluster;
    QPushButton *m_topBarReturnToEditButton;
    QPushButton *m_topBarLandButton;
    QPushButton *m_topBarRtlButton;
    QPushButton *m_topBarForceDisarmButton;
    QPushButton *m_topBarFlightTermButton;
    
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
    QPushButton *m_missionPlayButton;
    QPushButton *m_missionPauseContinueButton;
    QPushButton *m_createModeButton;
    QPushButton *m_addCurvePointButton = nullptr;
    QPushButton *m_transformModeButton;
    QPushButton *m_playPathPreviewButton;
    QPushButton *m_stopPathPreviewButton;
    QPushButton *m_previewDecorationsButton;
    QLineEdit *m_pathNameEdit;
    QLabel *m_missionStatusLabel;

    // Curve-mode state
    bool  m_nextWaypointIsCurve = false;
    float m_nextCurveDefaultRadiusM = 1.5f;
    
    // View controls
    QPushButton *m_resetCameraButton;
    QPushButton *m_viewModeButton;
    QDoubleSpinBox *m_defaultAltitudeSpinBox;
    QPushButton *m_undoEditButton;
    QPushButton *m_redoEditButton;
    QString m_mapperMapPath;
    QString m_mapperMapBundleDir;
    QString m_backgroundBundleJsonPath;
    QString m_backgroundBundleFolderName;
    QString m_plannerPathsDir; ///< Volume override; empty = use plannerPathsDirectory()
    QString m_roomId;
    QString m_roomName;
    QString m_roomMapDir;

    QTimer *m_pathPreviewAnimationTimer;
    int m_pathPreviewWaypointIndex;
    float m_pathPreviewProgress;
    bool m_isPlayingPathPreview;

    // Current waypoint selection
    int m_selectedWaypoint;  // ID of selected waypoint (-1 if none)
    
    // Drone connection
    DroneController *m_droneController;
    bool m_hasUploadedSnapshot;
    bool m_waypointsDirtySinceUpload;
    bool m_editingLocked;
    QString m_uploadedWaypointFingerprint;
    QString m_lastControllerError;
    QString m_lastMissionStatusText;

    bool m_updatingWaypointTable;
    bool m_reorderingWaypointRows;

    void removeMapperHomeWaypointAndRefresh();
};

#endif // PATHPLANNERWIDGET_H
