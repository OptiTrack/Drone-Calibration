#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QMutex>
#include <QVector3D>
#include <QMap>
#include <QSet>
#include "connection_controller.h"
#include "mesh.h"
#include "meshGenerator.h"
#include "src/controllers/configurecontroller.h"

struct RigidBodyOffsets {
    int                              bodyID;
    QVector<QVector3D>               markerOffsets;  // local (offset) positions
};

struct GLWidgetAssets {
    QVector<QVector<QPair<int, int>>> skeletons;    // An array of skeletons with each skeleton as an array of bones from the parent bone index to the child index
    QVector<RigidBodyOffsets> rbOffsets;            // Marker offsets from the center of the rigid body

    // constructor to make the compiler happy
    GLWidgetAssets(
        const QVector<QVector<QPair<int,int>>>& s,
        const QVector<RigidBodyOffsets>&        r
    ) : skeletons(s), rbOffsets(r) {}
};

class GLWidget : public QOpenGLWidget,
                 protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget() override;

    /**
     * @brief Assigns the ConnectionController to this widget and hooks up frame signals.
     *        After calling this, the widget will start rendering incoming frames.
     */
    void setController(ConnectionController *controller);

    /**
     * @brief Returns the current OpenGL rendering assets (skeletons and rigid body offsets).
     */
    GLWidgetAssets getAssets();

    /**
     * @brief Sets the OpenGL rendering assets used for drawing skeletons and rigid bodies.
     * @param assets The GLWidgetAssets containing skeleton structure and rigid body marker offsets.
     */
    void setAssets(GLWidgetAssets assets);

    void selectAsset(AssetSettings assets);

public slots:
    /**
     * @brief Slot called when new frame data is available from the ConnectionController.
     *        Grabs the latest FrameData and triggers a repaint.
     */
    void onFramesUpdated(FrameData frame);

protected:
    /**
     * @brief Catches mouse press on rendering window and updates state based
     *        on the button pressed
     */
    void mousePressEvent(QMouseEvent *e) override;

    /**
     * @brief Catches mouse release on rendering window and updates state based
     *        on the button pressed
     */
    void mouseReleaseEvent(QMouseEvent *e) override;

    /**
     * @brief Catches mouse movement and pans or rotates if a button is currently
     *        pressed
     */
    void mouseMoveEvent(QMouseEvent* e) override;

    /**
     * @brief Catches scroll wheel movement and zooms in or out
     */
    void wheelEvent(QWheelEvent *e) override;

    /**
     * @brief
     */
    void timerEvent(QTimerEvent *e) override;

    /**
     * @brief Configures OpenGL settings, binds/links shader program, and
     *        initializes the VBOs for each mesh.
     */
    void initializeGL() override;

    /**
     * @brief Handles resizing of the rendering window
     */
    void resizeGL(int w, int h) override;

    /**
     * @brief Updates the VBOs for each mesh and paints to the rendering window
     */
    void paintGL() override;

private:
    /**
     * @brief Builds the list of (parentIndex, childIndex) bone pairs for each skeleton
     *        by querying the NatNet data descriptions.
     */
    void initSceneDescriptions();

    /**
     * @brief Updates the view matrix based on current camera parameters
     */
    void updateViewMatrix();

    /**
     * @brief Initializes VBO for a grid to resemble the floor
     */
    void initGrid();

    /**
     * @brief Initializes VBO for the rotation indicator
     */
    void initRotationIndicator();

    /**
     * @brief Renders the ground grid lines in the scene
     */
    void drawGrid();

    /**
     * @brief Extracts bone line segments and joint positions from the latest frame
     * 
     * @param lineData Output container for bone line segments
     * @param jointData Output container for unique joint positions
     */
    void prepareSkeletonData(QVector<QVector3D>& lineData, QVector<QVector3D>& jointData);

    void prepareRigidBodies(std::vector<std::unique_ptr<Mesh>>& meshes);

    void drawRigidBodies();

    /**
     * @brief Draws the bones and joints of all skeletons
     * 
     * @param lineData Input bone segment positions to draw as lines
     * @param jointData Input joint positions to draw as points
     */
    void drawSkeletons(const QVector<QVector3D>& lineData, const QVector<QVector3D>& jointData);

    /**
     * @brief Renders a small 3D axis indicator in the bottom-left corner of the viewport
     */
    void drawAxisIndicator();

    // Controller and data members
    QOpenGLShaderProgram m_prog{this};                  // Shader program
    ConnectionController *m_controller = nullptr;       // Connnection controller instance
    bool m_skeletonReady = false;                       // Current state of connection data descriptions
    // true = data descriptions have been loaded
    
    // OpenGL objects
    QVector<QVector<QPair<int, int>>> m_skeletonBones;              // Bone pairs for each skeleton
    QVector<RigidBodyOffsets> m_rbOffsets;                          // Rigid body offsets for each rb
    QOpenGLBuffer m_rigidBodiesVBO{QOpenGLBuffer::VertexBuffer};    // For rigid body
    QOpenGLBuffer m_gridMinorVBO{QOpenGLBuffer::VertexBuffer};      // For minor gridlines
    QOpenGLBuffer m_gridMajorVBO{QOpenGLBuffer::VertexBuffer};      // For major gridlines
    QOpenGLBuffer m_axisVBO{ QOpenGLBuffer::VertexBuffer };         // For axis indicator
    MeshGenerator m_mg;
    Mesh m_boneMesh;
    Mesh m_jointMesh;
    std::vector<std::unique_ptr<Mesh>> m_rigidBodyMeshes;
    AssetSettings m_selectedAssets;
    float m_boneRadius = .04;
    float m_jointRadius = .05;
    int m_minorGridLineCount = 0;                                   // Minor gridline count
    int m_majorGridLineCount = 0;                                   // Major gridline count
    int m_axisLineCount = 0;                                        // Total axis line count
    int m_rbIndexCount;

    QMatrix4x4 m_proj;    // Projection state
    QMatrix4x4 m_view;    // View matrix state

    // Panning and zoom
    bool    m_panning = false;      // Panning current state
    QPoint  m_lastPanPos;           // Last panning position
    float   m_panX = 0.0f;          // Pan x axis
    float   m_panY = 0.0f;          // Pan y axis
    float   m_panZ = 0.0f;          // Pan z axis
    float   m_panSpeed = 0.002f;
    float   m_zoom = 1.0f;          // Zoom state

    // Rotation
    bool    m_rotating      = false;    // Rotation current state
    QPoint  m_lastRotPos;               // Last rotation position
    const float kRotSpeed   = 0.2f;     // tweak sensitivity
    float     m_yaw         = 0.0f;
    float     m_pitch       = 0.0f;
    
    QMutex    m_frameMutex;         // Mutex lock for frame data
    FrameData m_latestFrame;        // Most recent FrameData received from connection
};

#endif // GLWIDGET_H