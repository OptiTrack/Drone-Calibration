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
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTimer>
#include <QMouseEvent>
#include <QLineEdit>
#include <QWheelEvent>
#include <QPaintEvent>
#include <QDialog>
#include <vector>
#include "../models/waypoint.h"
#include "../models/flightpath.h"

class PathRenderer;
class DroneController;

QT_BEGIN_NAMESPACE
namespace Ui { class PathPlannerWidget; }
QT_END_NAMESPACE

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

signals:
    void waypointSelected(int id);
    void waypointAdded(const Waypoint &waypoint);
    void waypointMoved(int id, const QVector3D &newPosition);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupShaders();
    void setupBuffers();
    void drawGrid();
    void drawWaypoints();
    void drawPath();
    void drawAxes();
    void drawWaypointLabels(QPainter &painter);
    void updateCamera();
    void updateProjection();
    QVector3D screenToWorld(const QPoint &screenPos, float depth = 0.0f);
    QPoint worldToScreen(const QVector3D &worldPos);
    int findWaypointAt(const QPoint &screenPos);
    
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
    float m_orthoZoom;
    float m_defaultAltitude;
    
    // Waypoints
    std::vector<Waypoint> m_waypoints;
    int m_selectedWaypoint;  // ID of selected waypoint (-1 if none)
    
    // Interaction
    QPoint m_lastMousePos;
    bool m_mousePressed;
    bool m_isDragging;
    
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
    void onAddWaypoint();
    void onRemoveWaypoint();
    void onClearPath();
    void onSavePath();
    void onLoadPath();
    void onUploadMission();
    void onRunMission();
    void onCancelMission();
    void onMissionUploadComplete(bool success, const QString &message);
    void onMissionStatusReceived(const QString &status);
    void onWaypointSelected(int id);
    void onWaypointCellChanged(int row, int column);
    void onCameraReset();
    void onPlayPath();
    void onStopPath();
    void onPathAnimationTimer();
    void onGridSizeChanged(int size);
    void onCoordinateSystemChanged(const QString &system);
    void onViewModeChanged();
    void onSequentialOrder();
    void onCustomOrder();
    void onUndoReorder();

    // Quick Missions
    void onGenerateSquare();
    void onLoadRecording();

private:
    void setupUI();
    void setupControls();
    void setupWaypointTable();
    void setupQuickMissions();
    void updateWaypointTable();
    void startPathAnimation();
    void stopPathAnimation();
    void emitWaypointsChanged();
    void updatePathOrderVisibility();
    
    Ui::PathPlannerWidget *ui;
    
    // Main layouts
    QHBoxLayout *m_mainLayout;
    QVBoxLayout *m_controlsLayout;
    
    // 3D View
    PathPlannerOpenGLWidget *m_openglWidget;
    
    // Control panels
    QGroupBox *m_waypointGroup;
    QGroupBox *m_pathGroup;
    QGroupBox *m_pathOrderGroup;
    QGroupBox *m_viewGroup;
    QGroupBox *m_settingsGroup;
    
    // Waypoint controls
    QTableWidget *m_waypointTable;
    QPushButton *m_addWaypointButton;
    QPushButton *m_removeWaypointButton;
    QLabel *m_waypointCountLabel;
    
    // Path controls
    QPushButton *m_clearPathButton;
    QPushButton *m_savePathButton;
    QPushButton *m_loadPathButton;
    QPushButton *m_uploadMissionButton;
    QPushButton *m_runMissionButton;
    QPushButton *m_cancelMissionButton;
    QPushButton *m_playPathButton;
    QPushButton *m_stopPathButton;
    QLineEdit *m_pathNameEdit;
    QLabel *m_pathLengthLabel;
    QLabel *m_missionStatusLabel;
    
    // Path order controls
    QPushButton *m_sequentialOrderButton;
    QPushButton *m_customOrderButton;
    QPushButton *m_undoReorderButton;
    std::vector<Waypoint> m_previousWaypointOrder;
    
    // View controls
    QPushButton *m_resetCameraButton;
    QPushButton *m_viewModeButton;
    QSlider *m_gridSizeSlider;
    QComboBox *m_coordinateSystemCombo;
    QDoubleSpinBox *m_defaultAltitudeSpinBox;
    
    // Animation
    QTimer *m_pathAnimationTimer;
    int m_currentAnimationWaypoint;
    float m_animationProgress;
    bool m_isPlayingPath;
    
    // Current waypoint selection
    int m_selectedWaypoint;  // ID of selected waypoint (-1 if none)
    
    // Drone connection
    DroneController *m_droneController;

    // Quick Missions panel
    QGroupBox       *m_quickMissionsGroup;
    QPushButton     *m_generateSquareButton;
    QDoubleSpinBox  *m_squareAltSpinBox;
    QDoubleSpinBox  *m_squareSideSpinBox;
    QPushButton     *m_loadRecordingButton;
    QLabel          *m_quickMissionStatusLabel;
};

#endif // PATHPLANNERWIDGET_H