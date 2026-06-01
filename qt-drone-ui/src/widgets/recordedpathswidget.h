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
#include <QVector3D>
#include "../models/flightpath.h"

class VolumeManager;

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
    void refreshRooms();

signals:
    void pathDeleted(const QString &pathId);
    void pathLoadRequested(const QVector<QVector3D> &points);
    /// Full planner JSON (waypoints + mapper_map_path); preferred when the list entry came from disk.
    void pathJsonLoadRequested(const QString &absoluteJsonPath);

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
    void onImportLegacyPaths();
    void onCleanupLegacyPaths();

private:
    void setupRoomControls();
    void setupConnections();
    void updatePathList();
    void updatePathDetails();
    void clearPathDetails();
    FlightPath* getSelectedPath();
    QString getPathsDirectory();
    QStringList legacyPathsDirectories() const;
    bool copyPathIntoCurrentRoom(const QString &sourcePath, QString *destPath = nullptr);
    bool writeJsonPreservingPlannerFields(const QString &destPath, const FlightPath &path,
                                          const QJsonObject &sourceRoot = QJsonObject()) const;
    FlightPath loadPathFromFile(const QString &filePath);
    void updateRoomSummary();
    
    Ui::RecordedPathsWidget *ui;
    QString m_pathsDirectory;
    VolumeManager *m_volumeManager;
    QComboBox *m_roomCombo;
    QPushButton *m_newRoomButton;
    QPushButton *m_renameRoomButton;
    QPushButton *m_importLegacyButton;
    QPushButton *m_cleanupLegacyButton;
    QLabel *m_roomMapLabel;
    
    // Data
    QVector<FlightPath> m_paths;
    int m_selectedPathIndex;
};

#endif // RECORDEDPATHSWIDGET_H