#include "GLWidget.h"
#include <QOpenGLShader>
#include <QMutexLocker>
#include <QString>
#include <QMouseEvent>
#include <QTimer>
#include <QVector3D>
#include <cmath>

GLWidget::GLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    // Start the animation timer
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&GLWidget::update));
    timer->start(16); // ~60 FPS
}

GLWidget::~GLWidget()
{
    makeCurrent();
    // Free
    doneCurrent();
}

GLWidgetAssets GLWidget::getAssets()
{
    return GLWidgetAssets(m_skeletonBones, m_rbOffsets);
}

void GLWidget::setAssets(GLWidgetAssets assets)
{
    m_rbOffsets.clear();
    m_skeletonBones = assets.skeletons;
    m_rbOffsets = assets.rbOffsets;

    m_skeletonReady = true;
}

void GLWidget::selectAsset(AssetSettings assets)
{
    m_selectedAssets = assets;
    qDebug() << "Skeleton:" << assets.skeleton << "RigidBody:" << assets.rigidBody;
}

void GLWidget::setController(ConnectionController *controller)
{
    m_controller = controller;
    if (m_controller)
    {
        // Hook up frame updates
        QObject::connect(m_controller, &ConnectionController::framesUpdated,
                         this, &GLWidget::onFramesUpdated);
        QObject::connect(m_controller, &ConnectionController::sendMaps,
                         this, &GLWidget::initSceneDescriptions);
    }
}

void GLWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton)
    {
        // begin rotation
        m_rotating = true;
        m_lastRotPos = e->pos();
    }
    else if (e->button() == Qt::MiddleButton)
    {
        // begin panning (unchanged)
        m_panning = true;
        m_lastPanPos = e->pos();
    }
    // ignore left-button now
    e->accept();
}

void GLWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning)
    {
        // Calculate pan delta in screen space, scaled by zoom
        QPoint delta = e->pos() - m_lastPanPos;
        m_lastPanPos = e->pos();

        // normalize pan speed to current zoom
        float factor = m_zoom * m_panSpeed;

        // derive camera‐right in the XZ plane from yaw:
        float yRad = qDegreesToRadians(m_yaw);
        float cosY = std::cos(yRad);
        float sinY = std::sin(yRad);
        QVector3D right{cosY, 0, -sinY}; // unit‐length if yaw/pitch kept normalized

        // move left/right along camera‐right,
        // and up/down along world‐Y
        m_panX -= right.x() * delta.x() * factor;
        m_panZ -= right.z() * delta.x() * factor;
        m_panY += delta.y() * factor;

        // Apply the updated pan to the view matrix
        updateViewMatrix();
        update(); // schedule a repaint
    }
    else if (m_rotating)
    {
        // Calculate rotation deltas (yaw and pitch) from mouse movement
        QPoint delta = e->pos() - m_lastRotPos;
        m_yaw -= delta.x() * kRotSpeed;   // horizontal drag -> yaw
        m_pitch += delta.y() * kRotSpeed; // vertical drag -> pitch

        // clamp pitch to prevent roll
        m_pitch = qBound(-89.0f, m_pitch, +89.0f);

        m_lastRotPos = e->pos();
        updateViewMatrix();
        update(); // schedule a repaint
    }
    else
    {
        // neither pan nor rotate
        e->ignore();
    }
}

void GLWidget::mouseReleaseEvent(QMouseEvent *e)
{
    // toggle state based on button released
    if (e->button() == Qt::RightButton)
    {
        m_rotating = false;
    }
    else if (e->button() == Qt::MiddleButton)
    {
        m_panning = false;
    }
    e->accept();
}

void GLWidget::wheelEvent(QWheelEvent *e)
{
    // angleDelta().y() is a multiple of 120 per notch
    float delta = e->angleDelta().y() / 120.0f;
    // Exponential zoom step per notch
    m_zoom /= std::pow(1.1f, delta);
    // Constrain zoom to a reasonable range
    m_zoom = qBound(0.1f, m_zoom, 10.0f);

    e->accept();
    // Recompute view matrix and repaint with new zoom
    updateViewMatrix();
    update();
}

void GLWidget::timerEvent(QTimerEvent *)
{
    update();
}

void GLWidget::updateViewMatrix()
{
    float radius = 2.0f * m_zoom;
    // Convert degrees to radians:
    float yRad = qDegreesToRadians(m_yaw);
    float pRad = qDegreesToRadians(m_pitch);

    QVector3D eye{
        radius * std::cos(pRad) * std::sin(yRad) + m_panX,
        radius * std::sin(pRad) + m_panY,
        radius * std::cos(pRad) * std::cos(yRad) + m_panZ};

    QVector3D center{m_panX, m_panY, m_panZ};

    m_view.setToIdentity();
    m_view.lookAt(
        eye,               // camera position
        center,            // scene center relative to current pan
        QVector3D(0, 1, 0) // constrain camera y axis to prevent roll
    );
}

void GLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    // Set background color (RBGA)
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);

    // Enable depth testing for proper 3D rendering
    glEnable(GL_DEPTH_TEST);

    // Enable point size for joint rendering
    glEnable(GL_PROGRAM_POINT_SIZE);

    // Enable line smoothing for nicer bones
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    // Compile shaders
    if (!m_prog.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/vshader.glsl"))
    {
        qWarning() << "Failed to compile vertex shader:" << m_prog.log();
    }

    if (!m_prog.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/fshader.glsl"))
    {
        qWarning() << "Failed to compile fragment shader:" << m_prog.log();
    }

    m_prog.bindAttributeLocation("a_position", 0);
    m_prog.bindAttributeLocation("a_normal",   1);

    if (!m_prog.link())
    {
        qWarning() << "Shader Program Link Error:" << m_prog.log();
    }

    m_mg = MeshGenerator();
    m_mg.cylinder(m_boneMesh);
    m_mg.sphere(m_jointMesh);

    // initialize constant mesh VBOs
    initGrid();
    initRotationIndicator();

    // Initialize camera position
    m_zoom = 4.0f;
    m_yaw  = 30.0f;
    m_pitch= 20.0f;
    updateViewMatrix();
}

void GLWidget::initGrid()
{
    const int extentMajor = 5;       // ±5 meters
    const float majorSpacing = 1.0f; // 1m between heavy lines
    const int minorCount = 4;        // 4 light subdivisions
    const float minorSpacing = majorSpacing / (minorCount + 1);
    const float gridHalf = extentMajor * majorSpacing; // half of the size of the grid

    QVector<QVector3D> minorLines;
    QVector<QVector3D> majorLines;

    // Minor gridlines
    // draw minor gridlines along x axis, stepping across the z axis w/ correct spacing
    for (float z = -gridHalf; z <= gridHalf; z += minorSpacing)
    {
        minorLines << QVector3D(-gridHalf, 0, z) << QVector3D(gridHalf, 0, z);
    }
    // draw minor gridlines along z axis, stepping across the x axis w/ correct spacing
    for (float x = -gridHalf; x <= gridHalf; x += minorSpacing)
    {
        minorLines << QVector3D(x, 0, -gridHalf) << QVector3D(x, 0, gridHalf);
    }

    // Minor gridlines
    // draw major gridlines along x axis, stepping across the z axis w/ correct spacing
    for (float z = -gridHalf; z <= gridHalf; z += majorSpacing)
    {
        majorLines << QVector3D(-gridHalf, 0, z) << QVector3D(gridHalf, 0, z);
    }
    // draw major gridlines along z axis, stepping across the x axis w/ correct spacing
    for (float x = -gridHalf; x <= gridHalf; x += majorSpacing)
    {
        majorLines << QVector3D(x, 0, -gridHalf) << QVector3D(x, 0, gridHalf);
    }

    // upload minor gridlines
    m_gridMinorVBO.create();
    m_gridMinorVBO.bind();
    m_gridMinorVBO.allocate(minorLines.constData(),
                            minorLines.size() * sizeof(QVector3D));
    m_gridMinorVBO.release();
    m_minorGridLineCount = minorLines.size();

    // upload major gridlines
    m_gridMajorVBO.create();
    m_gridMajorVBO.bind();
    m_gridMajorVBO.allocate(majorLines.constData(),
                            majorLines.size() * sizeof(QVector3D));
    m_gridMajorVBO.release();
    m_majorGridLineCount = majorLines.size();
}

void GLWidget::initRotationIndicator()
{
    QVector<QVector3D> axisLines = {
        {0, 0, 0},
        {1, 0, 0},
        {0, 0, 0},
        {0, 1, 0},
        {0, 0, 0},
        {0, 0, 1},
    };
    m_axisLineCount = axisLines.size();
    m_axisVBO.create();
    m_axisVBO.bind();
    m_axisVBO.allocate(axisLines.constData(),
                       axisLines.size() * sizeof(QVector3D));
    m_axisVBO.release();
}

void GLWidget::resizeGL(int w, int h)
{
    float aspect = float(w) / float(h);
    m_proj.setToIdentity();
    m_proj.perspective(45.0f, aspect, 0.1f, 100.0f);
}

void GLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_controller)
        return;

    // Lazy initialize skeleton VBOs and descriptions
    if (!m_skeletonReady)
    {
        const auto &descriptions = m_controller->getDataDescriptions();
        if (descriptions)
        {
            initSceneDescriptions();
            m_skeletonReady = true;
        }
        else
        {
            return;
        }
    }

    m_prog.bind();
    m_prog.setUniformValue("view",  m_view);
    m_prog.setUniformValue("proj",  m_proj);

    // Draw grid lines
    drawGrid();

    // Prepare and draw skeleton bones and joints
    QVector<QVector3D> boneData, jointData;
    prepareSkeletonData(boneData, jointData);
    drawSkeletons(boneData, jointData);

    prepareRigidBodies(m_rigidBodyMeshes);


    // Draw them as thin lines
    drawRigidBodies();

    // Draw 3D axis orientation indicator
    drawAxisIndicator();

    m_prog.release();
}

void GLWidget::onFramesUpdated(FrameData frame)
{
    if (!m_controller)
        return;
        
    m_latestFrame = frame;

    update();
}

void GLWidget::initSceneDescriptions()
{
    qDebug() << "GLWidget: Reloading Scene";
    // Retrieve NatNet data descriptions
    sDataDescriptions *desc = m_controller->getDataDescriptions();
    if (!desc)
        return;

    int nDescs = desc->nDataDescriptions;
    int skeletonCount = 0, rigidBodyCount = 0;

    // First count skeletons
    for (int i = 0; i < nDescs; ++i)
    {
        if (desc->arrDataDescriptions[i].type == Descriptor_Skeleton)
        {
            skeletonCount++;
        } else if (desc->arrDataDescriptions[i].type == Descriptor_RigidBody) {
            rigidBodyCount++;
        }
    }

    m_skeletonBones.resize(skeletonCount);
    for (auto& meshPtr : m_rigidBodyMeshes)
    {
        Mesh& mesh = *meshPtr;
        mesh.clear();
    }
    m_rbOffsets.clear();


    // Build (parent, child) index pairs for each skeleton
    int skelIdx = 0;
    for (int i = 0; i < nDescs; ++i)
    {
        if (desc->arrDataDescriptions[i].type == Descriptor_Skeleton)
        {
            auto &skelDesc = desc->arrDataDescriptions[i].Data.SkeletonDescription;

            // Map from bone ID to index in the skeleton's RigidBodyData array
            QMap<int, int> boneIDtoIndex;
            for (int j = 0; j < skelDesc->nRigidBodies; ++j)
            {
                boneIDtoIndex[skelDesc->RigidBodies[j].ID] = j;
            }

            // Create parent-child pairs
            for (int j = 0; j < skelDesc->nRigidBodies; ++j)
            {
                int parentID = skelDesc->RigidBodies[j].parentID;
                if (parentID != -1)
                {
                    int parentIndex = boneIDtoIndex.value(parentID, -1);
                    if (parentIndex != -1)
                    {
                        m_skeletonBones[skelIdx].append({parentIndex, j});
                    }
                }
            }

            skelIdx++;
        } else if (desc->arrDataDescriptions[i].type == Descriptor_RigidBody)
        {
            auto &rbDesc = desc->arrDataDescriptions[i].Data.RigidBodyDescription;

            // compute centroid of all markerPositions
            int nM = rbDesc->nMarkers;
            QVector3D centroid(0,0,0);
            for (int m = 0; m < nM; ++m) {
                float* mp = rbDesc->MarkerPositions[m]; 
                centroid += QVector3D(mp[0], mp[1], mp[2]);
            }
            centroid /= float(nM);

            // store offsets = markerPosition - centroid
            RigidBodyOffsets ro;
            ro.bodyID = rbDesc->ID;
            ro.markerOffsets.reserve(nM);
            for (int m = 0; m < nM; ++m) {
                float* mp = rbDesc->MarkerPositions[m];
                QVector3D markerLocal(mp[0], mp[1], mp[2]);
                ro.markerOffsets.push_back(markerLocal - centroid);
            }

            m_rbOffsets.push_back(ro);
        }
    }
}


void GLWidget::prepareSkeletonData(QVector<QVector3D> &boneData, QVector<QVector3D> &jointData)
{
    // Track added joints to avoid duplicates
    QSet<int> addedJoints;
    
    // Lock frame data for thread safety
    QMutexLocker lock(&m_frameMutex);
    const auto &skeletons = m_latestFrame.skeletons;
    
    for (int s = 0; s < skeletons.size(); ++s)
    {
        const auto &skel = skeletons[s];
        m_prog.setUniformValue("skeleton_id", float(s));
        
        // Iterate through all bones in the skeleton
        for (const auto &bone : m_skeletonBones[s])
        {
            const auto &parent = skel.bones[bone.first];
            const auto &child = skel.bones[bone.second];
            
            QVector3D parentPos(parent.position.x(), parent.position.y(), parent.position.z());
            QVector3D childPos(child.position.x(), child.position.y(), child.position.z());
            
            boneData.append(parentPos);
            boneData.append(childPos);
            
            if (!addedJoints.contains(bone.first))
            {
                jointData.append(parentPos);
                addedJoints.insert(bone.first);
            }
            if (!addedJoints.contains(bone.second))
            {
                jointData.append(childPos);
                addedJoints.insert(bone.second);
            }
        }
    }
}

void GLWidget::drawSkeletons(const QVector<QVector3D> &boneData, const QVector<QVector3D> &jointData)
{
    // Set lighting direction
    m_prog.bind();
    m_prog.setUniformValue("lightDir", QVector3D(-0.5f, -1.0f, -0.3f).normalized());
    
    // Draw bones as cylinders
    m_prog.setUniformValue("render_mode", 0);
    for (int i = 0; i < boneData.size(); i += 2) {
        QVector3D P = boneData[i];
        QVector3D C = boneData[i+1];
        
        // compute transform
        QVector3D dir   = C - P;
        float     len   = dir.length();
        QVector3D mid   = (P + C) * 0.5f;
        QQuaternion rot = QQuaternion::rotationTo({0,1,0}, dir.normalized());
        
        QMatrix4x4 model;
        model.translate(mid);
        model.rotate(rot);
        model.scale(m_boneRadius, len, m_boneRadius);
        
        m_prog.setUniformValue("model", model);
        
        // draw
        m_boneMesh.vao().bind();
        glDrawElements(GL_TRIANGLES,
            m_boneMesh.indexCount(),
                          GL_UNSIGNED_INT,
                          nullptr);
        m_boneMesh.vao().release();
    }

    // Draw joints as spheres
    m_prog.setUniformValue("render_mode", 1);
    const float headJointRadius = m_jointRadius * 2;
    for (int i = 0; i < jointData.size(); ++i) {
        const QVector3D &J = jointData[i];
        float r = (i == 4 ? headJointRadius : m_jointRadius);
        
        QMatrix4x4 model;
        model.translate(J);
        model.scale(r, r, r);
        m_prog.setUniformValue("model", model);
        
        m_jointMesh.vao().bind();
        glDrawElements(GL_TRIANGLES,
                        m_jointMesh.indexCount(),
                        GL_UNSIGNED_INT,
                        nullptr);
        m_jointMesh.vao().release();
    }
    
    m_prog.release();
}

void GLWidget::prepareRigidBodies(std::vector<std::unique_ptr<Mesh>>& meshes)
{
    meshes.clear();
    // Build world‐space rigid‐body markers & lines
    std::vector<QVector3D>  rbPoints;
    std::vector<uint32_t>   rbIndices;
    
    const auto& rbFrameList = m_latestFrame.rigidBodies; 
    
    // For each precomputed RigidBodyOffsets, find matching frame data
    for (const auto& ro : m_rbOffsets)
    {
        auto meshPtr = std::make_unique<Mesh>();
        Mesh& mesh = *meshPtr;

        rbPoints.clear();
        rbIndices.clear();
        // find the matching live data by ID
        const RigidBodyData* dataPtr = nullptr;
        for (const auto& rd : rbFrameList) {
            if (rd.id == ro.bodyID) {
                dataPtr = &rd;
                break;
            }
        }
        if (!dataPtr) continue;
        
        // compute body’s world transform
        QVector3D  bodyPos(dataPtr->position.x(), dataPtr->position.y(), dataPtr->position.z());
        QQuaternion bodyRot(dataPtr->orientation.scalar(),
                            dataPtr->orientation.x(),
                            dataPtr->orientation.y(),
                            dataPtr->orientation.z());
        
        // rotate each local offset, then add translation
        for (const auto& off : ro.markerOffsets) {
            QVector3D worldMarker = bodyPos + bodyRot.rotatedVector(off);
            rbPoints.push_back(worldMarker);
        }
        
        // TODO: Optimize for optimal loop speed
        // connect “all‐pairs” within this rigid body
        int nM    = ro.markerOffsets.size();
        for (int i = 0; i < nM; ++i) {
            for (int j = i+1; j < nM; ++j) {
                rbIndices.push_back(i);
                rbIndices.push_back(j);
            }
        }
        m_mg.wireframe(mesh, rbPoints, rbIndices);
        mesh.setIndexCount(rbIndices.size());
        mesh.setIdAndType( dataPtr->id,QString("Rigid Body"));
        meshes.push_back(std::move(meshPtr));
    }
}

void GLWidget::drawRigidBodies()
{
    m_prog.bind();
    const auto rbNames = m_controller->getRigidBodyIdToName();
    for (auto& meshPtr : m_rigidBodyMeshes)
    {
        Mesh& mesh = *meshPtr;
        if (mesh.indexCount() == 0) continue;
        
        // Decide if this mesh is the selected rigid body:
        bool isSelected = false;
        auto it = rbNames.find(mesh.id());
        if (it != rbNames.end() && it->second == m_selectedAssets.rigidBody) {
            isSelected = true;
        }

        // If not selected, draw a single pass at render_mode = 5.0
        if (!isSelected) {
            m_prog.setUniformValue("render_mode", 5);
            glLineWidth(2.0f);

            QMatrix4x4 model;
            model.setToIdentity();
            m_prog.setUniformValue("model", model);

            mesh.vao().bind();
            glDrawElements(GL_LINES,
                           mesh.indexCount(),
                           GL_UNSIGNED_INT,
                           nullptr);
            mesh.vao().release();
        }
        else {
            // If it is selected, do two passes:

            // Silhouette (outline)
            {
                m_prog.setUniformValue("render_mode", 6);
                glLineWidth(3.0f); 

                QMatrix4x4 model;
                model.setToIdentity();
                m_prog.setUniformValue("model", model);

                mesh.vao().bind();
                glDrawElements(GL_LINES,
                               mesh.indexCount(),
                               GL_UNSIGNED_INT,
                               nullptr);
                mesh.vao().release();
            }

            // Fill
            {
                m_prog.setUniformValue("render_mode", 5);
                glLineWidth(2.0f); 

                QMatrix4x4 model;
                model.setToIdentity();
                m_prog.setUniformValue("model", model);

                mesh.vao().bind();
                glDrawElements(GL_LINES,
                               mesh.indexCount(),
                               GL_UNSIGNED_INT,
                               nullptr);
                mesh.vao().release();
            }
        }
    }

    m_prog.release();

    // Restore default line width for any subsequent drawing:
    glLineWidth(1.0f);
}
    
void GLWidget::drawGrid()
{
    QMatrix4x4 model;

    m_prog.setUniformValue("model", model);

    // Draw minor grid (light gray)
    m_prog.setUniformValue("render_mode", 2);
    m_gridMinorVBO.bind();
    m_prog.enableAttributeArray(0);
    m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    glLineWidth(.1f);
    glDrawArrays(GL_LINES, 0, m_minorGridLineCount);
    m_prog.disableAttributeArray(0);
    m_gridMinorVBO.release();

    // Draw major grid (brighter gray)
    m_prog.setUniformValue("render_mode", 3);
    m_gridMajorVBO.bind();
    m_prog.enableAttributeArray(0);
    m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    glLineWidth(2.f);
    glDrawArrays(GL_LINES, 0, m_majorGridLineCount);
    m_prog.disableAttributeArray(0);
    m_gridMajorVBO.release();
}

void GLWidget::drawAxisIndicator()
{
    
    // Save the current viewport and enable scissor test to restrict rendering to the corner
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    glEnable(GL_SCISSOR_TEST);
    
    const int size = 100;
    glViewport(10, 10, size, size);
    glScissor(10, 10, size, size);
    // Clear only the depth buffer in the small viewport
    glClear(GL_DEPTH_BUFFER_BIT);
    
    // Set up a simple perspective projection for the axis
    QMatrix4x4 axisProj;
    axisProj.setToIdentity();
    axisProj.perspective(45.0f, 1.0f, 0.1f, 10.0f);

    QQuaternion invYaw = QQuaternion::fromAxisAndAngle(0, 1, 0, -m_yaw);
    QQuaternion invPitch = QQuaternion::fromAxisAndAngle(1, 0, 0, m_pitch);
    QQuaternion axisRot = invPitch * invYaw;
    
    // Place camera to look at the origin and rotate based on current view
    QMatrix4x4 axisView;
    axisView.setToIdentity();
    axisView.translate(0, 0, -3.0f);
    axisView.rotate(axisRot);
    
    m_prog.bind();
    m_prog.setUniformValue("view", axisView);
    m_prog.setUniformValue("proj", axisProj);
    m_prog.setUniformValue("model", QMatrix4x4());  // identity model
    m_prog.setUniformValue("render_mode", 4);
    
    m_axisVBO.bind();
    m_prog.enableAttributeArray(0);
    m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    
    for (int i = 0; i < 3; ++i)
    {
        switch (i)
        {
            case 0:
            m_prog.setUniformValue("axis_color", QVector3D(1, 0, 0));
            break;
            case 1:
            m_prog.setUniformValue("axis_color", QVector3D(0, 1, 0));
            break;
            case 2:
            m_prog.setUniformValue("axis_color", QVector3D(0, 0, 1));
            break;
        }
        glLineWidth(1.0f);
        glDrawArrays(GL_LINES, i * 2, 2);
    }

    m_prog.disableAttributeArray(0);
    m_axisVBO.release();
    m_prog.release();
    glDisable(GL_SCISSOR_TEST);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}
