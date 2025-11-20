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

struct FlightPath {
    QString id;
    QString name;
    QVector<QVector3D> points;
    qint64 createdAt;
    QString description;
    
    QJsonObject toJson() const;
    static FlightPath fromJson(const QJsonObject &json);
};

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
    void savePaths();

signals:
    void pathDeleted(const QString &pathId);
    void pathLoadRequested(const QVector<QVector3D> &points);

private slots:
    void onPathSelectionChanged();
    void onDeletePath();
    void onLoadPath();
    void onExportPath();
    void onImportPath();
    void onEditPath();
    void onDuplicatePath();

private:
    void setupUI();
    void updatePathList();
    void updatePathDetails();
    void clearPathDetails();
    FlightPath* getSelectedPath();
    QString generatePathId();
    
    Ui::RecordedPathsWidget *ui;
    
    // UI Components
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_contentLayout;
    
    // Path list
    QGroupBox *m_pathListGroup;
    QVBoxLayout *m_pathListLayout;
    QListWidget *m_pathList;
    QHBoxLayout *m_pathButtonsLayout;
    QPushButton *m_deleteButton;
    QPushButton *m_loadButton;
    QPushButton *m_exportButton;
    QPushButton *m_importButton;
    QPushButton *m_duplicateButton;
    
    // Path details
    QGroupBox *m_pathDetailsGroup;
    QVBoxLayout *m_pathDetailsLayout;
    QLabel *m_pathNameLabel;
    QLabel *m_pathCreatedLabel;
    QLabel *m_pathPointCountLabel;
    QLabel *m_pathLengthLabel;
    QTextEdit *m_pathDescriptionEdit;
    QPushButton *m_editPathButton;
    QListWidget *m_waypointDetailsList;
    
    // Data
    QVector<FlightPath> m_paths;
    int m_selectedPathIndex;
};

#endif // RECORDEDPATHSWIDGET_H