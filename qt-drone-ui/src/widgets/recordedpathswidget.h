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
#include <QJsonObject>
#include <QJsonArray>
#include <QVector3D>
#include "../models/flightpath.h"

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

private:
    void setupConnections();
    void updatePathList();
    void updatePathDetails();
    void clearPathDetails();
    FlightPath* getSelectedPath();
    QString getPathsDirectory();
    FlightPath loadPathFromFile(const QString &filePath);
    
    Ui::RecordedPathsWidget *ui;
    QString m_pathsDirectory;
    
    // Data
    QVector<FlightPath> m_paths;
    int m_selectedPathIndex;
};

#endif // RECORDEDPATHSWIDGET_H