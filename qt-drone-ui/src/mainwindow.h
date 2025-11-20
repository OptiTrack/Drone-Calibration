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
    void onRecordingSaved(const QString &filePath, const QByteArray &data);
    void onRecordingDeleted(const QString &recordingId);
    void onRecordingPlayRequested(const QString &filePath);

private:
    void setupUI();
    void setupNavigationBar();
    void setupMainContent();
    void setupStatusBar();
    void connectSignals();
    void setActiveView(const QString &viewName);
    
    Ui::MainWindow *ui;
    
    // UI Components
    QWidget *m_centralWidget;
    QHBoxLayout *m_mainLayout;
    QFrame *m_navigationFrame;
    QVBoxLayout *m_navigationLayout;
    QListWidget *m_navigationList;
    QPushButton *m_drawerToggleButton;
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
    
    // State
    bool m_drawerOpen;
    QString m_activeView;
    QVector<QVector3D> m_draftPoints;
};

#endif // MAINWINDOW_H