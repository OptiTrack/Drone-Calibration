#pragma once

#include <QObject>
#include <QVector>
#include <QTimer>
#include "frame_data.h"
#include "glwidget.h"

class DataProcessor;

/**
 * @brief Controls replay functionality for recorded motion capture data.
 *
 * This class manages playback of saved frames, loading from JSON files,
 * and interfacing with rendering and data processing components. It also
 * handles recording of streamed or replayed takes.
 */
class ReplayController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the ReplayController.
     * @param parent Optional parent QObject.
     */
    explicit ReplayController(QObject* parent = nullptr);

    /**
     * @brief Sets the data processor used to compute metrics.
     * @param processor Pointer to the data processor.
     */
    void setDataProcessor(DataProcessor* processor);

    /**
     * @brief Sets the OpenGL widget for visual rendering.
     * @param widget Pointer to the GLWidget instance.
     */
    void setOpenGLWidget(GLWidget* widget);

    /**
     * @brief Loads frames into the controller for replay.
     * @param frames A vector of saved FrameData.
     */
    void setSavedFrames(const QVector<FrameData>& frames);

    /**
     * @brief Stops the current replay if active.
     */
    void stopReplay();

public slots:
    /**
     * @brief Loads a commonly formatted take for replay.
     * @param filename The file to load.
     * @param playspeed The playback speed setting.
     */
    void loadCommonTake(QString filename, QString playspeed);

    /**
     * @brief Loads a previously saved take.
     * @param filename The file to load.
     * @param playspeed The playback speed setting.
     */
    void loadSavedTake(QString filename, QString playspeed);

    /**
     * @brief Starts replay of the loaded frames.
     */
    void startReplay();

    /**
     * @brief Saves the current replayed frames.
     */
    void saveTake();

    /**
     * @brief Sets recording mode for streamed data.
     * @param ConnectionSettings Connection info for the data stream.
     * @param isRecording Whether recording is active.
     */
    void recordStream(ConnectionSettings ConnectionSettings, bool isRecording);

    /**
     * @brief Sets recording mode for replayed data.
     * @param isRecording Whether recording is active.
     */
    void recordReplay(bool isRecording);

    /**
     * @brief Saves data if it was recorded during a stream.
     */
    void saveStream();

    /**
     * @brief Saves data if it was recorded during a replay.
     */
    void saveReplay();


signals:
    /**
     * @brief Signal emitted with the current frame to replay.
     * @param frame The frame being replayed.
     */
    void replayFrame(FrameData frame);

    /**
     * @brief Signal to load rigid body, skeleton, and bone ID maps.
     * @param rigidBodyMap ID to name map for rigid bodies.
     * @param skeletonMap ID to name map for skeletons.
     * @param boneMap Nested map from skeletonID to bone ID and name.
     */
    void loadReplayMaps(std::unordered_map<int, std::string> rigidBodyMap, 
                        std::unordered_map<int, std::string> skeletonMap,
                        std::unordered_map<int, std::unordered_map<int, std::string>> boneMap);

    /**
     * @brief Signal to indicate whether a common take was successfully loaded.
     * @param isReady Whether the take is ready.
     */
    bool commonTakeReady(bool isReady);

    /**
     * @brief Signal to indicate whether a saved take was successfully loaded.
     * @param isReady Whether the take is ready.
     */
    bool savedTakeReady(bool isReady);

    /**
     * @brief Signal emitted when a new take is saved.
     */
    void newSavedTake();


private slots:
    /**
     * @brief Emits the next frame in the replay sequence.
     */
    void emitNextFrame();

private:
    QVector<FrameData> m_savedFrames;  // Stored frames for replay.
    int m_currentIndex = 0;            // Index of the current replay frame.
    QTimer m_timer;                    // Timer to control frame playback.
    bool m_isReplaying = false;        // Whether replay is active.
    int m_intervalMs = 1;              // Time interval between frames (ms).
    bool m_isRecording = false;        // Whether recording is enabled.

    QElapsedTimer m_lastFrameTime;     // Timer for tracking real-time playback.

    DataProcessor* m_dataProcessor = nullptr; // Pointer to the data processor.
    GLWidget* m_openGLWidget = nullptr;       // Pointer to the OpenGL widget.

    /**
     * @brief Loads a JSON file from disk.
     * @param path Path to the JSON file.
     * @param outRoot Output root object to populate.
     * @return True if load was successful, false otherwise.
     */
    bool loadJsonFile(const QString& path, QJsonObject& outRoot);

    /**
     * @brief Parses frames from a JSON array into FrameData objects.
     * @param framesJson JSON array of frame objects.
     */
    void parseFrames(const QJsonArray& framesJson);

    /**
     * @brief Parses identifier-to-name maps from JSON.
     * @param root Root object containing mapping data.
     */
    void parseIdMaps(const QJsonObject& root);

    /**
     * @brief Parses OpenGL assets used in rendering.
     * @param glAssetsObj JSON object containing asset data.
     */
    void parseGLAssets(const QJsonObject& glAssetsObj);
};
