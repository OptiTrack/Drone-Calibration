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
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>

class PathRenderer;

QT_BEGIN_NAMESPACE
namespace Ui { class PathPlannerWidget; }
QT_END_NAMESPACE

class PathPlannerOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit PathPlannerOpenGLWidget(QWidget *parent = nullptr);
    ~PathPlannerOpenGLWidget();

    void setWaypoints(const QVector<QVector3D> &waypoints);
    void addWaypoint(const QVector3D &point);
    void removeWaypoint(int index);
    void clearWaypoints();
    void setSelectedWaypoint(int index);
    QVector<QVector3D> getWaypoints() const { return m_waypoints; }

signals:
    void waypointSelected(int index);
    void waypointAdded(const QVector3D &point);
    void waypointMoved(int index, const QVector3D &newPosition);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
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
    void updateCamera();
    QVector3D screenToWorld(const QPoint &screenPos, float depth = 0.0f);
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
    
    // Waypoints
    QVector<QVector3D> m_waypoints;
    int m_selectedWaypoint;
    
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

    void loadPoints(const QVector<QVector3D> &points);
    void clearPath();

signals:
    void pathSaved(const QString &name, const QVector<QVector3D> &points);

private slots:
    void onAddWaypoint();
    void onRemoveWaypoint();
    void onClearPath();
    void onSavePath();
    void onLoadPath();
    void onWaypointSelected(int index);
    void onWaypointPositionChanged();
    void onCameraReset();
    void onPlayPath();
    void onStopPath();
    void onPathAnimationTimer();
    void onGridSizeChanged(int size);
    void onCoordinateSystemChanged(const QString &system);

private:
    void setupUI();
    void setupControls();
    void setupWaypointList();
    void updateWaypointList();
    void updateWaypointControls();
    void validateAndUpdateWaypoint();
    void startPathAnimation();
    void stopPathAnimation();
    
    Ui::PathPlannerWidget *ui;
    
    // Main layouts
    QHBoxLayout *m_mainLayout;
    QVBoxLayout *m_controlsLayout;
    
    // 3D View
    PathPlannerOpenGLWidget *m_openglWidget;
    
    // Control panels
    QGroupBox *m_waypointGroup;
    QGroupBox *m_pathGroup;
    QGroupBox *m_viewGroup;
    QGroupBox *m_settingsGroup;
    
    // Waypoint controls
    QListWidget *m_waypointList;
    QPushButton *m_addWaypointButton;
    QPushButton *m_removeWaypointButton;
    QDoubleSpinBox *m_xSpinBox;
    QDoubleSpinBox *m_ySpinBox;
    QDoubleSpinBox *m_zSpinBox;
    QLabel *m_waypointCountLabel;
    
    // Path controls
    QPushButton *m_clearPathButton;
    QPushButton *m_savePathButton;
    QPushButton *m_loadPathButton;
    QPushButton *m_playPathButton;
    QPushButton *m_stopPathButton;
    QLineEdit *m_pathNameEdit;
    QLabel *m_pathLengthLabel;
    
    // View controls
    QPushButton *m_resetCameraButton;
    QSlider *m_gridSizeSlider;
    QComboBox *m_coordinateSystemCombo;
    
    // Animation
    QTimer *m_pathAnimationTimer;
    int m_currentAnimationWaypoint;
    float m_animationProgress;
    bool m_isPlayingPath;
    
    // Current waypoint selection
    int m_selectedWaypoint;
};

#endif // PATHPLANNERWIDGET_H