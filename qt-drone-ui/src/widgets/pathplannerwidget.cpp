#include "pathplannerwidget.h"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QListWidgetItem>
#include <QtMath>
#include <QOpenGLShader>

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
    : QOpenGLWidget(parent)
    , m_shaderProgram(nullptr)
    , m_cameraPosition(0, 5, 10)
    , m_cameraTarget(0, 0, 0)
    , m_cameraUp(0, 1, 0)
    , m_cameraDistance(15.0f)
    , m_cameraYaw(0.0f)
    , m_cameraPitch(30.0f)
    , m_selectedWaypoint(-1)
    , m_mousePressed(false)
    , m_isDragging(false)
    , m_animationTime(0.0f)
{
    setMinimumSize(600, 400);
    setFocusPolicy(Qt::StrongFocus);
    
    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(16); // ~60 FPS
    connect(m_animationTimer, &QTimer::timeout, [this]() {
        m_animationTime += 0.016f;
        update();
    });
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
    glEnable(GL_POINT_SMOOTH);
    glEnable(GL_LINE_SMOOTH);
    
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    
    setupShaders();
    setupBuffers();
}

void PathPlannerOpenGLWidget::setupShaders()
{
    m_shaderProgram = new QOpenGLShaderProgram(this);
    m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    m_shaderProgram->link();
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
    
    float aspect = float(width) / float(height);
    m_projectionMatrix.setToIdentity();
    m_projectionMatrix.perspective(45.0f, aspect, 0.1f, 100.0f);
}

void PathPlannerOpenGLWidget::updateCamera()
{
    // Update camera position based on spherical coordinates
    float x = m_cameraDistance * cos(qDegreesToRadians(m_cameraPitch)) * cos(qDegreesToRadians(m_cameraYaw));
    float y = m_cameraDistance * sin(qDegreesToRadians(m_cameraPitch));
    float z = m_cameraDistance * cos(qDegreesToRadians(m_cameraPitch)) * sin(qDegreesToRadians(m_cameraYaw));
    
    m_cameraPosition = QVector3D(x, y, z) + m_cameraTarget;
    
    m_viewMatrix.setToIdentity();
    m_viewMatrix.lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);
}

void PathPlannerOpenGLWidget::drawGrid()
{
    QVector<float> gridVertices;
    QVector<float> gridColors;
    
    float gridSize = 20.0f;
    float gridSpacing = 1.0f;
    
    // Grid lines parallel to X axis
    for (float z = -gridSize; z <= gridSize; z += gridSpacing) {
        gridVertices << -gridSize << 0.0f << z;
        gridVertices << gridSize << 0.0f << z;
        
        float alpha = (z == 0.0f) ? 0.8f : 0.3f;
        gridColors << 0.5f << 0.5f << 0.5f;
        gridColors << 0.5f << 0.5f << 0.5f;
    }
    
    // Grid lines parallel to Z axis
    for (float x = -gridSize; x <= gridSize; x += gridSpacing) {
        gridVertices << x << 0.0f << -gridSize;
        gridVertices << x << 0.0f << gridSize;
        
        float alpha = (x == 0.0f) ? 0.8f : 0.3f;
        gridColors << 0.5f << 0.5f << 0.5f;
        gridColors << 0.5f << 0.5f << 0.5f;
    }
    
    if (!gridVertices.isEmpty()) {
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

void PathPlannerOpenGLWidget::drawAxes()
{
    QVector<float> axesVertices = {
        // X axis (red)
        0.0f, 0.0f, 0.0f,  2.0f, 0.0f, 0.0f,
        // Y axis (green) 
        0.0f, 0.0f, 0.0f,  0.0f, 2.0f, 0.0f,
        // Z axis (blue)
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 2.0f
    };
    
    QVector<float> axesColors = {
        // X axis (red)
        1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,
        // Y axis (green)
        0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        // Z axis (blue)
        0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f
    };
    
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
    
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, 6);
    glLineWidth(1.0f);
    
    colorBuffer.release();
    m_vertexBuffer.release();
    m_vao.release();
}

void PathPlannerOpenGLWidget::drawWaypoints()
{
    if (m_waypoints.isEmpty()) return;
    
    QVector<float> waypointVertices;
    QVector<float> waypointColors;
    
    for (int i = 0; i < m_waypoints.size(); ++i) {
        const QVector3D &wp = m_waypoints[i];
        waypointVertices << wp.x() << wp.y() << wp.z();
        
        if (i == m_selectedWaypoint) {
            waypointColors << 0.2f << 0.6f << 1.0f; // Blue for selected
        } else {
            waypointColors << 0.2f << 0.8f << 0.2f; // Green for normal
        }
    }
    
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
    
    glPointSize(10.0f);
    glDrawArrays(GL_POINTS, 0, m_waypoints.size());
    
    colorBuffer.release();
    m_vertexBuffer.release();
    m_vao.release();
}

void PathPlannerOpenGLWidget::drawPath()
{
    if (m_waypoints.size() < 2) return;
    
    QVector<float> pathVertices;
    QVector<float> pathColors;
    
    for (int i = 0; i < m_waypoints.size() - 1; ++i) {
        const QVector3D &wp1 = m_waypoints[i];
        const QVector3D &wp2 = m_waypoints[i + 1];
        
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
    
    glLineWidth(2.0f);
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
    
    if (event->button() == Qt::LeftButton) {
        int waypointIndex = findWaypointAt(event->pos());
        if (waypointIndex >= 0) {
            setSelectedWaypoint(waypointIndex);
            emit waypointSelected(waypointIndex);
        } else if (event->modifiers() & Qt::ControlModifier) {
            // Add new waypoint
            QVector3D worldPos = screenToWorld(event->pos());
            addWaypoint(worldPos);
            emit waypointAdded(worldPos);
        }
    }
}

void PathPlannerOpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_mousePressed) return;
    
    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();
    
    if (event->buttons() & Qt::RightButton) {
        // Camera rotation
        m_cameraYaw += delta.x() * 0.5f;
        m_cameraPitch -= delta.y() * 0.5f;
        
        m_cameraPitch = qBound(-89.0f, m_cameraPitch, 89.0f);
        
        update();
    } else if (event->buttons() & Qt::MiddleButton) {
        // Camera panning
        float sensitivity = 0.01f;
        QVector3D right = QVector3D::crossProduct(m_cameraTarget - m_cameraPosition, m_cameraUp).normalized();
        QVector3D up = QVector3D::crossProduct(right, m_cameraTarget - m_cameraPosition).normalized();
        
        m_cameraTarget += right * delta.x() * sensitivity;
        m_cameraTarget += up * delta.y() * sensitivity;
        
        update();
    }
}

void PathPlannerOpenGLWidget::wheelEvent(QWheelEvent *event)
{
    float delta = event->angleDelta().y() / 120.0f;
    m_cameraDistance -= delta * 0.5f;
    m_cameraDistance = qBound(2.0f, m_cameraDistance, 50.0f);
    update();
}

QVector3D PathPlannerOpenGLWidget::screenToWorld(const QPoint &screenPos, float depth)
{
    // Simple screen to world conversion for ground plane (y=0)
    // This is a simplified version - in a full implementation you'd use proper unprojection
    float x = (screenPos.x() - width() / 2.0f) / (width() / 20.0f);
    float z = (screenPos.y() - height() / 2.0f) / (height() / 20.0f);
    return QVector3D(x, depth, -z);
}

int PathPlannerOpenGLWidget::findWaypointAt(const QPoint &screenPos)
{
    // Simple hit testing - in a full implementation you'd use proper 3D picking
    for (int i = 0; i < m_waypoints.size(); ++i) {
        // Project waypoint to screen and check distance
        // This is simplified - proper implementation would use the projection matrices
        QVector3D wp = m_waypoints[i];
        QPoint projected(width() / 2 + wp.x() * width() / 20.0f, 
                        height() / 2 - wp.z() * height() / 20.0f);
        
        if ((projected - screenPos).manhattanLength() < 20) {
            return i;
        }
    }
    return -1;
}

void PathPlannerOpenGLWidget::setWaypoints(const QVector<QVector3D> &waypoints)
{
    m_waypoints = waypoints;
    update();
}

void PathPlannerOpenGLWidget::addWaypoint(const QVector3D &point)
{
    m_waypoints.append(point);
    update();
}

void PathPlannerOpenGLWidget::removeWaypoint(int index)
{
    if (index >= 0 && index < m_waypoints.size()) {
        m_waypoints.removeAt(index);
        if (m_selectedWaypoint == index) {
            m_selectedWaypoint = -1;
        } else if (m_selectedWaypoint > index) {
            m_selectedWaypoint--;
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

void PathPlannerOpenGLWidget::setSelectedWaypoint(int index)
{
    m_selectedWaypoint = index;
    update();
}

// PathPlannerWidget Implementation
PathPlannerWidget::PathPlannerWidget(QWidget *parent)
    : QWidget(parent)
    , ui(nullptr)
    , m_mainLayout(nullptr)
    , m_controlsLayout(nullptr)
    , m_openglWidget(nullptr)
    , m_waypointGroup(nullptr)
    , m_pathGroup(nullptr)
    , m_viewGroup(nullptr)
    , m_settingsGroup(nullptr)
    , m_waypointList(nullptr)
    , m_selectedWaypoint(-1)
    , m_currentAnimationWaypoint(0)
    , m_animationProgress(0.0f)
    , m_isPlayingPath(false)
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
    setupWaypointList();
    
    // Connect signals
    connect(m_openglWidget, &PathPlannerOpenGLWidget::waypointSelected,
            this, &PathPlannerWidget::onWaypointSelected);
    connect(m_openglWidget, &PathPlannerOpenGLWidget::waypointAdded,
            this, [this](const QVector3D &point) {
                updateWaypointList();
                onWaypointSelected(m_openglWidget->getWaypoints().size() - 1);
            });
}

void PathPlannerWidget::setupControls()
{
    // Waypoint group
    m_waypointGroup = new QGroupBox("Waypoints");
    m_waypointGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
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
    
    // Position controls
    QGridLayout *positionLayout = new QGridLayout;
    positionLayout->addWidget(new QLabel("X:"), 0, 0);
    positionLayout->addWidget(new QLabel("Y:"), 1, 0);
    positionLayout->addWidget(new QLabel("Z:"), 2, 0);
    
    m_xSpinBox = new QDoubleSpinBox;
    m_xSpinBox->setRange(-100, 100);
    m_xSpinBox->setSingleStep(0.1);
    m_xSpinBox->setDecimals(1);
    
    m_ySpinBox = new QDoubleSpinBox;
    m_ySpinBox->setRange(0, 20);
    m_ySpinBox->setSingleStep(0.1);
    m_ySpinBox->setDecimals(1);
    
    m_zSpinBox = new QDoubleSpinBox;
    m_zSpinBox->setRange(-100, 100);
    m_zSpinBox->setSingleStep(0.1);
    m_zSpinBox->setDecimals(1);
    
    positionLayout->addWidget(m_xSpinBox, 0, 1);
    positionLayout->addWidget(m_ySpinBox, 1, 1);
    positionLayout->addWidget(m_zSpinBox, 2, 1);
    waypointLayout->addLayout(positionLayout);
    
    // Path group
    m_pathGroup = new QGroupBox("Path");
    m_pathGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
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
    
    // View group
    m_viewGroup = new QGroupBox("View");
    m_viewGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    m_controlsLayout->addWidget(m_viewGroup);
    
    QVBoxLayout *viewLayout = new QVBoxLayout(m_viewGroup);
    
    m_resetCameraButton = new QPushButton("Reset Camera");
    viewLayout->addWidget(m_resetCameraButton);
    
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
    connect(m_playPathButton, &QPushButton::clicked, this, &PathPlannerWidget::onPlayPath);
    connect(m_stopPathButton, &QPushButton::clicked, this, &PathPlannerWidget::onStopPath);
    connect(m_resetCameraButton, &QPushButton::clicked, this, &PathPlannerWidget::onCameraReset);
    
    connect(m_xSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PathPlannerWidget::onWaypointPositionChanged);
    connect(m_ySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PathPlannerWidget::onWaypointPositionChanged);
    connect(m_zSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PathPlannerWidget::onWaypointPositionChanged);
    
    connect(m_gridSizeSlider, &QSlider::valueChanged, this, &PathPlannerWidget::onGridSizeChanged);
    connect(m_coordinateSystemCombo, &QComboBox::currentTextChanged, this, &PathPlannerWidget::onCoordinateSystemChanged);
}

void PathPlannerWidget::setupWaypointList()
{
    m_waypointList = new QListWidget;
    m_waypointList->setMaximumHeight(150);
    m_waypointList->setStyleSheet(
        "QListWidget { background-color: #1f2937; color: white; border: 1px solid #4b5563; } "
        "QListWidget::item { padding: 4px; border-bottom: 1px solid #374151; } "
        "QListWidget::item:hover { background-color: #374151; } "
        "QListWidget::item:selected { background-color: #3b82f6; }"
    );
    
    // Insert waypoint list after waypoint count label
    QVBoxLayout *waypointLayout = qobject_cast<QVBoxLayout*>(m_waypointGroup->layout());
    waypointLayout->insertWidget(1, m_waypointList);
    
    connect(m_waypointList, &QListWidget::currentRowChanged, this, &PathPlannerWidget::onWaypointSelected);
}

void PathPlannerWidget::onAddWaypoint()
{
    QVector3D newPoint(0, 2, 0);
    m_openglWidget->addWaypoint(newPoint);
    updateWaypointList();
    onWaypointSelected(m_openglWidget->getWaypoints().size() - 1);
}

void PathPlannerWidget::onRemoveWaypoint()
{
    if (m_selectedWaypoint >= 0) {
        m_openglWidget->removeWaypoint(m_selectedWaypoint);
        updateWaypointList();
        m_selectedWaypoint = -1;
        updateWaypointControls();
    }
}

void PathPlannerWidget::onClearPath()
{
    m_openglWidget->clearWaypoints();
    updateWaypointList();
    m_selectedWaypoint = -1;
    updateWaypointControls();
}

void PathPlannerWidget::onSavePath()
{
    QString name = m_pathNameEdit->text().trimmed();
    if (name.isEmpty()) {
        name = "Untitled Path";
    }
    
    QVector<QVector3D> waypoints = m_openglWidget->getWaypoints();
    if (!waypoints.isEmpty()) {
        emit pathSaved(name, waypoints);
    }
}

void PathPlannerWidget::onLoadPath()
{
    QString fileName = QFileDialog::getOpenFileName(this, 
        "Load Path", 
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "JSON Files (*.json)");
    
    if (!fileName.isEmpty()) {
        // TODO: Implement path loading from JSON
        QMessageBox::information(this, "Load Path", "Path loading not yet implemented");
    }
}

void PathPlannerWidget::onWaypointSelected(int index)
{
    m_selectedWaypoint = index;
    m_openglWidget->setSelectedWaypoint(index);
    m_waypointList->setCurrentRow(index);
    updateWaypointControls();
}

void PathPlannerWidget::onWaypointPositionChanged()
{
    if (m_selectedWaypoint >= 0) {
        QVector<QVector3D> waypoints = m_openglWidget->getWaypoints();
        if (m_selectedWaypoint < waypoints.size()) {
            waypoints[m_selectedWaypoint] = QVector3D(
                m_xSpinBox->value(),
                m_ySpinBox->value(), 
                m_zSpinBox->value()
            );
            m_openglWidget->setWaypoints(waypoints);
            updateWaypointList();
        }
    }
}

void PathPlannerWidget::onCameraReset()
{
    // Reset camera to default position - this would be implemented in the OpenGL widget
    m_openglWidget->update();
}

void PathPlannerWidget::onPlayPath()
{
    if (!m_isPlayingPath && !m_openglWidget->getWaypoints().isEmpty()) {
        startPathAnimation();
    }
}

void PathPlannerWidget::onStopPath()
{
    if (m_isPlayingPath) {
        stopPathAnimation();
    }
}

void PathPlannerWidget::onPathAnimationTimer()
{
    // Simple path animation logic
    m_animationProgress += 0.02f;
    if (m_animationProgress >= 1.0f) {
        m_animationProgress = 0.0f;
        m_currentAnimationWaypoint++;
        
        QVector<QVector3D> waypoints = m_openglWidget->getWaypoints();
        if (m_currentAnimationWaypoint >= waypoints.size()) {
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

void PathPlannerWidget::updateWaypointList()
{
    m_waypointList->clear();
    
    QVector<QVector3D> waypoints = m_openglWidget->getWaypoints();
    for (int i = 0; i < waypoints.size(); ++i) {
        const QVector3D &wp = waypoints[i];
        QString text = QString("WP %1: (%2, %3, %4)")
                      .arg(i + 1)
                      .arg(wp.x(), 0, 'f', 1)
                      .arg(wp.y(), 0, 'f', 1)
                      .arg(wp.z(), 0, 'f', 1);
        m_waypointList->addItem(text);
    }
    
    m_waypointCountLabel->setText(QString("Count: %1").arg(waypoints.size()));
    
    // Calculate path length
    float totalLength = 0.0f;
    for (int i = 0; i < waypoints.size() - 1; ++i) {
        totalLength += waypoints[i].distanceToPoint(waypoints[i + 1]);
    }
    m_pathLengthLabel->setText(QString("Length: %1 m").arg(totalLength, 0, 'f', 1));
}

void PathPlannerWidget::updateWaypointControls()
{
    QVector<QVector3D> waypoints = m_openglWidget->getWaypoints();
    bool hasSelection = m_selectedWaypoint >= 0 && m_selectedWaypoint < waypoints.size();
    
    m_removeWaypointButton->setEnabled(hasSelection);
    m_xSpinBox->setEnabled(hasSelection);
    m_ySpinBox->setEnabled(hasSelection);
    m_zSpinBox->setEnabled(hasSelection);
    
    if (hasSelection) {
        const QVector3D &wp = waypoints[m_selectedWaypoint];
        m_xSpinBox->blockSignals(true);
        m_ySpinBox->blockSignals(true);
        m_zSpinBox->blockSignals(true);
        
        m_xSpinBox->setValue(wp.x());
        m_ySpinBox->setValue(wp.y());
        m_zSpinBox->setValue(wp.z());
        
        m_xSpinBox->blockSignals(false);
        m_ySpinBox->blockSignals(false);
        m_zSpinBox->blockSignals(false);
    }
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
    m_openglWidget->setWaypoints(points);
    updateWaypointList();
    if (!points.isEmpty()) {
        onWaypointSelected(0);
    }
}

void PathPlannerWidget::clearPath()
{
    onClearPath();
}