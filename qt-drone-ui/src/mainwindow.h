#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QComboBox>
#include "utils/flightlogger.h"
#include "utils/volumemanager.h"

// Forward declarations
class CameraFeedWidget;
class PathPlannerWidget;
class RecordedPathsWidget;
class RecordedVideosWidget;
class DroneStatusWidget;
class DroneController;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onNavigationItemClicked(int index);
    void onDrawerToggled();
    void onPathSaved(const QString &name, const QVector<QVector3D> &points);
    void onPathDeleted(const QString &pathId);
    void onPathLoadRequested(const QVector<QVector3D> &points);
    void onPathJsonLoadRequested(const QString &absoluteJsonPath);
    void onRecordingSaved(const QString &filePath, const QByteArray &data);
    void onRecordingDeleted(const QString &recordingId);
    void onRecordingPlayRequested(const QString &filePath);
    void onVolumeComboChanged(int index);
    void onNewVolumeRequested();

private:
    void setupUI();
    void setupNavigationBar();
    void setupMainContent();
    void setupStatusBar();
    void setupPlannerMenus();
    void connectSignals();
    void setActiveView(const QString &viewName);
    void rebuildVolumeCombo();
    void applyActiveVolume(const VolumeManager::VolumeInfo &volume);
    
    Ui::MainWindow *ui;
    
    // UI Components
    QWidget *m_centralWidget;
    QHBoxLayout *m_mainLayout;
    QFrame *m_navigationFrame;
    QVBoxLayout *m_navigationLayout;
    QListWidget *m_navigationList;
    QPushButton *m_drawerToggleButton;
    QPushButton *m_reopenSidebarButton;
    QLabel *m_connectionStatusDot;
    QLabel *m_connectionStatusText;
    QStackedWidget *m_contentStack;
    QSplitter *m_mainSplitter;
    
    // Widget pages
    CameraFeedWidget *m_cameraFeedWidget;
    PathPlannerWidget *m_pathPlannerWidget;
    RecordedPathsWidget *m_recordedPathsWidget;
    RecordedVideosWidget *m_recordedVideosWidget;
    DroneStatusWidget *m_droneStatusWidget;
    
    // Controllers
    DroneController *m_droneController;
    FlightLogger    *m_flightLogger;
    VolumeManager   *m_volumeManager;

    // Volume selector (in status bar)
    QComboBox   *m_volumeCombo;
    QPushButton *m_newVolumeButton;
    QPushButton *m_logButton;      // Manual start/stop logging

    // State
    bool m_drawerOpen;
    QString m_activeView;
    QVector<QVector3D> m_draftPoints;
};

#endif // MAINWINDOW_H