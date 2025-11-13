#include "replay_controller.h"
#include "data_processor.h"
#include "glwidget.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QOpenGLWidget>
#include <QVector>


ReplayController::ReplayController(QObject* parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &ReplayController::emitNextFrame);
}

void ReplayController::setSavedFrames(const QVector<FrameData>& frames)
{
    m_savedFrames = frames;
    m_currentIndex = 0;
}

void ReplayController::startReplay()
{
    if (m_savedFrames.isEmpty()) {
        qWarning() << "ReplayController: No frames to replay.";
        return;
    }

    m_currentIndex = 0;
    m_isReplaying = true;

    m_lastFrameTime.start();  // QElapsedTimer
    QTimer::singleShot(0, this, &ReplayController::emitNextFrame);
}

void ReplayController::stopReplay()
{
    m_timer.stop();
    m_isReplaying = false;
}

void ReplayController::emitNextFrame()
{
    if (!m_isReplaying || m_currentIndex >= m_savedFrames.size()) {
        stopReplay();
        return;
    }

    emit replayFrame(m_savedFrames[m_currentIndex]);
    ++m_currentIndex;

    qint64 elapsed = m_lastFrameTime.elapsed();
    int delay = std::max(0, m_intervalMs - static_cast<int>(elapsed));
    m_lastFrameTime.restart();

    // Only continue if replay is still active
    if (m_isReplaying) {
        QTimer::singleShot(delay, this, &ReplayController::emitNextFrame);
    }

}

void ReplayController::setDataProcessor(DataProcessor* processor) {
    m_dataProcessor = processor;
}

void ReplayController::setOpenGLWidget(GLWidget* widget) {
    m_openGLWidget = widget;
}

void ReplayController::recordStream(ConnectionSettings ConnectionSettings, bool isRecording)
{
    m_isRecording = isRecording;
}

void ReplayController::recordReplay(bool isRecording)
{
    m_isRecording = isRecording;
}

void ReplayController::loadCommonTake(QString filename, QString playspeed)
{
    qDebug() << "Loading common take:" << filename;

    double scaleFactor = playspeed.remove('%').toDouble() / 100.0;
    m_intervalMs = static_cast<int>(1 / scaleFactor);

    QString filePath = ":json/src/assets/json/" + filename;

    QJsonObject root;
    if (!loadJsonFile(filePath, root)) {
        qWarning() << "Failed to load common take file.";
        return;
    }

    parseIdMaps(root);
    parseFrames(root["frames"].toArray());
    parseGLAssets(root["glAssets"].toObject());

    emit commonTakeReady(true);
}

void ReplayController::loadSavedTake(QString filename, QString playspeed)
{
    qDebug() << "Loading saved take:" << filename;

    double scaleFactor = playspeed.remove('%').toDouble() / 100.0;
    m_intervalMs = static_cast<int>(1 / scaleFactor);

    QString filePath = QCoreApplication::applicationDirPath() + "/saved_takes/" + filename;

    QJsonObject root;
    if (!loadJsonFile(filePath, root)) {
        qWarning() << "Failed to load saved take file.";
        return;
    }

    parseIdMaps(root);
    parseFrames(root["frames"].toArray());
    parseGLAssets(root["glAssets"].toObject());

    emit savedTakeReady(true);
}

bool ReplayController::loadJsonFile(const QString& path, QJsonObject& outRoot)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open JSON file:" << path;
        return false;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qWarning() << "JSON root is not an object in:" << path;
        return false;
    }

    outRoot = doc.object();
    return true;
}

void ReplayController::parseFrames(const QJsonArray& framesJson)
{
    m_savedFrames.clear();

    for (const QJsonValue& frameVal : framesJson) {
        QJsonObject frameObj = frameVal.toObject();

        FrameData frame;
        frame.frameNumber = frameObj["frameNumber"].toInt();
        frame.timestamp = frameObj["timestamp"].toDouble();

        // Rigid Bodies
        QJsonArray rigidBodiesJson = frameObj["rigidBodies"].toArray();
        for (const QJsonValue& rbVal : rigidBodiesJson) {
            QJsonObject rbObj = rbVal.toObject();
            RigidBodyData rb;
            rb.id = rbObj["id"].toInt();
            rb.parentId = rbObj["parentId"].toInt();

            QJsonArray pos = rbObj["position"].toArray();
            if (pos.size() == 3)
                rb.position = QVector3D(pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble());

            QJsonArray ori = rbObj["orientation"].toArray();
            if (ori.size() == 4)
                rb.orientation = QQuaternion(ori[3].toDouble(), ori[0].toDouble(), ori[1].toDouble(), ori[2].toDouble());

            frame.rigidBodies.push_back(rb);
        }

        // Skeletons
        QJsonArray skeletonsJson = frameObj["skeletons"].toArray();
        for (const QJsonValue& skelVal : skeletonsJson) {
            QJsonObject skelObj = skelVal.toObject();
            SkeletonData skeleton;
            skeleton.id = skelObj["id"].toInt();

            QJsonArray bonesJson = skelObj["bones"].toArray();
            for (const QJsonValue& boneVal : bonesJson) {
                QJsonObject boneObj = boneVal.toObject();
                RigidBodyData bone;
                bone.id = boneObj["id"].toInt();
                bone.parentId = boneObj["parentId"].toInt();

                QJsonArray pos = boneObj["position"].toArray();
                if (pos.size() == 3)
                    bone.position = QVector3D(pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble());

                QJsonArray ori = boneObj["orientation"].toArray();
                if (ori.size() == 4)
                    bone.orientation = QQuaternion(ori[3].toDouble(), ori[0].toDouble(), ori[1].toDouble(), ori[2].toDouble());

                skeleton.bones.push_back(bone);
            }

            frame.skeletons.push_back(skeleton);
        }

        m_savedFrames.push_back(frame);
    }

    qDebug() << "Parsed" << m_savedFrames.size() << "frames.";
}

void ReplayController::parseIdMaps(const QJsonObject& root)
{
    std::unordered_map<int, std::string> rigidBodyMap;
    QJsonObject rbMapJson = root.value("rigidBodies").toObject();
    for (const QString& key : rbMapJson.keys()) {
        rigidBodyMap[key.toInt()] = rbMapJson.value(key).toString().toStdString();
    }

    std::unordered_map<int, std::string> skeletonMap;
    QJsonObject skelMapJson = root.value("skeletons").toObject();
    for (const QString& key : skelMapJson.keys()) {
        skeletonMap[key.toInt()] = skelMapJson.value(key).toString().toStdString();
    }

    std::unordered_map<int, std::unordered_map<int, std::string>> boneMap;
    QJsonObject boneMapJson = root.value("bones").toObject();
    for (const QString& skeletonIdStr : boneMapJson.keys()) {
        int skeletonId = skeletonIdStr.toInt();
        QJsonObject boneSubMap = boneMapJson.value(skeletonIdStr).toObject();
        std::unordered_map<int, std::string> subMap;
        for (const QString& boneIdStr : boneSubMap.keys()) {
            subMap[boneIdStr.toInt()] = boneSubMap.value(boneIdStr).toString().toStdString();
        }
        boneMap[skeletonId] = subMap;
    }

    emit loadReplayMaps(rigidBodyMap, skeletonMap, boneMap);
}

void ReplayController::parseGLAssets(const QJsonObject& glAssetsObj)
{
    QVector<QVector<QPair<int, int>>> glSkeletons;
    QVector<RigidBodyOffsets> glRbOffsets;

    // Skeletons
    QJsonArray glSkeletonsJson = glAssetsObj.value("skeletons").toArray();
    for (const QJsonValue& skeletonVal : glSkeletonsJson) {
        QJsonArray boneArrayJson = skeletonVal.toArray();
        QVector<QPair<int, int>> bonePairs;
        for (const QJsonValue& pairVal : boneArrayJson) {
            QJsonArray pairJson = pairVal.toArray();
            if (pairJson.size() == 2) {
                bonePairs.append(QPair<int, int>{ pairJson[0].toInt(), pairJson[1].toInt() });
            }
        }
        glSkeletons.append(bonePairs);
    }

    // Rigid body marker offsets
    QJsonArray glRbOffsetsJson = glAssetsObj.value("rbOffsets").toArray();
    for (const QJsonValue& offsetVal : glRbOffsetsJson) {
        QJsonObject offsetObj = offsetVal.toObject();
        int bodyID = offsetObj["bodyID"].toInt();
        QVector<QVector3D> markerOffsets;

        QJsonArray markerArray = offsetObj["markerOffsets"].toArray();
        for (const QJsonValue& vecVal : markerArray) {
            QJsonArray vecArr = vecVal.toArray();
            if (vecArr.size() == 3) {
                markerOffsets.append(QVector3D(vecArr[0].toDouble(), vecArr[1].toDouble(), vecArr[2].toDouble()));
            }
        }

        glRbOffsets.append(RigidBodyOffsets{ bodyID, markerOffsets });
    }

    // Apply assets to GLWidget
    if (GLWidget* widget = qobject_cast<GLWidget*>(m_openGLWidget)) {
        widget->setAssets(GLWidgetAssets(glSkeletons, glRbOffsets));
        widget->update();
        qDebug() << "GLWidget assets updated.";
    } else {
        qWarning() << "ReplayController: Failed to cast m_openGLWidget to GLWidget.";
    }
}

void ReplayController::saveStream()
{
    if (m_isRecording){
        m_savedFrames = QVector<FrameData>(m_dataProcessor->getFrames().begin(), m_dataProcessor->getFrames().end());
        m_currentIndex = m_savedFrames.size();
        saveTake();
    }
}

void ReplayController::saveReplay()
{
    stopReplay();
    if (m_isRecording){
        saveTake();
    }
}

void ReplayController::saveTake()
{
qDebug() << "Saving frames and ID maps";

    QJsonObject root;

    // --- Save ID-to-name maps ---

    QJsonObject rigidMap;
    for (const auto& [id, name] : m_dataProcessor->getRigidBodyMap()) {
        rigidMap[QString::number(id)] = QString::fromStdString(name);
    }
    root["rigidBodies"] = rigidMap;

    QJsonObject skeletonMap;
    for (const auto& [id, name] : m_dataProcessor->getSkeletonNameMap()) {
        skeletonMap[QString::number(id)] = QString::fromStdString(name);
    }
    root["skeletons"] = skeletonMap;

    QJsonObject bonesMap;
    for (const auto& [skeletonId, boneMap] : m_dataProcessor->getBoneNameMap()) {
        QJsonObject boneObj;
        for (const auto& [boneId, boneName] : boneMap) {
            boneObj[QString::number(boneId)] = QString::fromStdString(boneName);
        }
        bonesMap[QString::number(skeletonId)] = boneObj;
    }
    root["bones"] = bonesMap;

    // --- Save frame data ---

    QJsonArray framesArray;

    for (int i = 0; i < m_currentIndex && i < m_savedFrames.size(); ++i) {
        const FrameData& frame = m_savedFrames[i];

        QJsonObject frameObj;
        frameObj["frameNumber"] = frame.frameNumber;
        frameObj["timestamp"] = frame.timestamp;

        QJsonArray rigidArray;
        for (const RigidBodyData& rb : frame.rigidBodies) {
            QJsonObject rbObj;
            rbObj["id"] = rb.id;
            rbObj["parentId"] = rb.parentId;

            QJsonArray posArray = { rb.position.x(), rb.position.y(), rb.position.z() };
            rbObj["position"] = posArray;

            QJsonArray orientArray = { rb.orientation.x(), rb.orientation.y(), rb.orientation.z(), rb.orientation.scalar() };
            rbObj["orientation"] = orientArray;

            rigidArray.append(rbObj);
        }
        frameObj["rigidBodies"] = rigidArray;

        QJsonArray skeletonArray;
        for (const SkeletonData& skeleton : frame.skeletons) {
            QJsonObject skeletonObj;
            skeletonObj["id"] = skeleton.id;

            QJsonArray bonesArray;
            for (const RigidBodyData& bone : skeleton.bones) {
                QJsonObject boneObj;
                boneObj["id"] = bone.id;
                boneObj["parentId"] = bone.parentId;

                QJsonArray posArray = { bone.position.x(), bone.position.y(), bone.position.z() };
                boneObj["position"] = posArray;

                QJsonArray orientArray = { bone.orientation.x(), bone.orientation.y(), bone.orientation.z(), bone.orientation.scalar() };
                boneObj["orientation"] = orientArray;

                bonesArray.append(boneObj);
            }
            skeletonObj["bones"] = bonesArray;
            skeletonArray.append(skeletonObj);
        }
        frameObj["skeletons"] = skeletonArray;

        framesArray.append(frameObj);
    }

    root["frames"] = framesArray;

    // --- Save GLWidget data ---

    GLWidgetAssets glassets = m_openGLWidget->getAssets();

    qDebug() << "Recieved gl skeletons" << glassets.skeletons;

    // Serialize skeletons
    QJsonArray skeletonsArray;
    for (const auto& skeleton : glassets.skeletons) {
        QJsonArray bonePairs;
        for (const auto& pair : skeleton) {
            QJsonArray pairArray;
            pairArray.append(pair.first);
            pairArray.append(pair.second);
            bonePairs.append(pairArray);
        }
        skeletonsArray.append(bonePairs);
    }

    // Serialize rbOffsets
    QJsonArray offsetsArray;
    for (const auto& offset : glassets.rbOffsets) {
        QJsonObject offsetObj;
        offsetObj["bodyID"] = offset.bodyID;

        QJsonArray markerArray;
        for (const auto& vec : offset.markerOffsets) {
            markerArray.append(QJsonArray{ vec.x(), vec.y(), vec.z() });
        }

    offsetObj["markerOffsets"] = markerArray;
    offsetsArray.append(offsetObj);
}

    // Combine into a GL assets object
    QJsonObject glAssetsObj;
    glAssetsObj["skeletons"] = skeletonsArray;
    glAssetsObj["rbOffsets"] = offsetsArray;

    // Add to root
    root["glAssets"] = glAssetsObj;

    QJsonDocument doc(root);
    // Get the program's directory
    QString appDir = QCoreApplication::applicationDirPath();
    QString saveDir = QDir(appDir).filePath("saved_takes");

    // Create the directory if it doesn't exist
    QDir().mkpath(saveDir);

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QString("take_%1.json").arg(timestamp);    
    QString savePath = QDir(saveDir).filePath(fileName);

    // Save to file
    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Full session saved to" << savePath;
    } else {
        qWarning() << "Failed to open file for saving:" << savePath;
    }

    m_isRecording = false;
    emit newSavedTake();
}