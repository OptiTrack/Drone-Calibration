#ifndef RECORDEDPATHSWIDGET_H
#define RECORDEDPATHSWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QGroupBox>
#include <QComboBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QDateTime>
#include <QTimer>
#include <QVector3D>
#include "../models/flightpath.h"

class VolumeManager;
class DroneController;

QT_BEGIN_NAMESPACE
namespace Ui { class RecordedPathsWidget; }
QT_END_NAMESPACE

class RecordedPathsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RecordedPathsWidget(QWidget *parent = nullptr);
    ~RecordedPathsWidget();

    void addPath(const QString &name, const QVector<QVector3D> &points);
    void loadPaths();
    void setVolumeManager(VolumeManager *volumeManager);
    void setDroneController(DroneController *controller);
    void refreshRooms();
    /// Refresh the map status label and Load Map button state for the current room.
    void updateRoomSummary();
    /// Poll the drone for map files referenced by the active room and saved paths.
    void refreshMapPresence();
    void refreshMapPresenceNow();

signals:
    void pathDeleted(const QString &pathId);
    void pathLoadRequested(const QVector<QVector3D> &points);
    /// Full planner JSON (waypoints + mapper_map_path); preferred when the list entry came from disk.
    void pathJsonLoadRequested(const QString &absoluteJsonPath);
    /// Request loading the room map from the drone (missions/.../ subdir).
    void mapLoadRequested(const QString &mapperSubdir);

private slots:
    void onPathSelectionChanged();
    void onDeletePath();
    void onLoadPath();
    void onExportPath();
    void onImportPath();
    void onEditPath();
    void onDuplicatePath();
    void onRoomSelectionChanged(int index);
    void onNewRoom();
    void onRenameRoom();
    void onLoadMap();

private:
    void setupRoomControls();
    void setupConnections();
    void updatePathList();
    void updatePathDetails();
    void clearPathDetails();
    FlightPath* getSelectedPath();
    QString getPathsDirectory();
    bool copyPathIntoCurrentRoom(const QString &sourcePath, QString *destPath = nullptr);
    bool writeJsonPreservingPlannerFields(const QString &destPath, const FlightPath &path,
                                          const QJsonObject &sourceRoot = QJsonObject()) const;
    FlightPath loadPathFromFile(const QString &filePath);
    QString mapperSubdirForPathFile(const QString &filePath) const;
    QString activeRoomMapperSubdir() const;
    QString resolveDroneMissionFolder(const QString &sanitizedBase) const;
    void handlePendingNewRoomName(const QString &name);
    
    Ui::RecordedPathsWidget *ui;
    QString m_pathsDirectory;
    VolumeManager *m_volumeManager;
    DroneController *m_droneController;
    QHash<QString, bool> m_remoteMapPresence;
    QSet<QString> m_mapPresencePollPending;
    QSet<QString> m_mapPresenceCheckFailed;
    QTimer *m_mapPresenceDebounce = nullptr;
    QString m_mapPresenceCachedSubdir;
    QDateTime m_mapPresenceCachedAt;
    QString m_pendingNewRoomName;
    QStringList m_droneMissionFolderNames;
    QComboBox *m_roomCombo;
    QPushButton *m_newRoomButton;
    QPushButton *m_renameRoomButton;
    QLabel *m_roomMapLabel;
    QPushButton *m_loadMapButton;

    // Data
    QVector<FlightPath> m_paths;
    int m_selectedPathIndex;
};

#endif // RECORDEDPATHSWIDGET_H