#ifndef RECORDEDVIDEOSWIDGET_H
#define RECORDEDVIDEOSWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QGroupBox>
#include <QJsonObject>
#include <QFileInfo>
#include <QDateTime>

struct Recording {
    QString id;
    QString name;
    QString filePath;
    qint64 fileSize;
    qint64 duration; // in seconds
    qint64 createdAt;
    QString format;
    QString quality;
    
    QJsonObject toJson() const;
    static Recording fromJson(const QJsonObject &json);
};

QT_BEGIN_NAMESPACE
namespace Ui { class RecordedVideosWidget; }
QT_END_NAMESPACE

class RecordedVideosWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RecordedVideosWidget(QWidget *parent = nullptr);
    ~RecordedVideosWidget();

    void addRecording(const QString &filePath, const QByteArray &data);
    void loadRecordings();
    void saveRecordings();

signals:
    void recordingDeleted(const QString &recordingId);
    void recordingPlayRequested(const QString &filePath);

private slots:
    void onRecordingSelectionChanged();
    void onPlayRecording();
    void onDeleteRecording();
    void onExportRecording();
    void onImportRecording();
    void onRefreshRecordings();

private:
    void setupConnections();
    void updateRecordingList();
    void updateRecordingDetails();
    void clearRecordingDetails();
    Recording* getSelectedRecording();
    QString generateRecordingId();
    QString formatFileSize(qint64 bytes);
    QString formatDuration(qint64 seconds);
    void updateStorageInfo();
    
    Ui::RecordedVideosWidget *ui;
    
    // Data
    QVector<Recording> m_recordings;
    int m_selectedRecordingIndex;
};

#endif // RECORDEDVIDEOSWIDGET_H