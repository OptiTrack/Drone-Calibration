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
    void setupUI();
    void updateRecordingList();
    void updateRecordingDetails();
    void clearRecordingDetails();
    Recording* getSelectedRecording();
    QString generateRecordingId();
    QString formatFileSize(qint64 bytes);
    QString formatDuration(qint64 seconds);
    
    Ui::RecordedVideosWidget *ui;
    
    // UI Components
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_contentLayout;
    
    // Recording list
    QGroupBox *m_recordingListGroup;
    QVBoxLayout *m_recordingListLayout;
    QListWidget *m_recordingList;
    QHBoxLayout *m_recordingButtonsLayout;
    QPushButton *m_playButton;
    QPushButton *m_deleteButton;
    QPushButton *m_exportButton;
    QPushButton *m_importButton;
    QPushButton *m_refreshButton;
    
    // Recording details
    QGroupBox *m_recordingDetailsGroup;
    QVBoxLayout *m_recordingDetailsLayout;
    QLabel *m_recordingNameLabel;
    QLabel *m_recordingCreatedLabel;
    QLabel *m_recordingFilePathLabel;
    QLabel *m_recordingFileSizeLabel;
    QLabel *m_recordingDurationLabel;
    QLabel *m_recordingFormatLabel;
    QLabel *m_recordingQualityLabel;
    
    // Storage info
    QGroupBox *m_storageGroup;
    QVBoxLayout *m_storageLayout;
    QLabel *m_totalRecordingsLabel;
    QLabel *m_totalSizeLabel;
    QProgressBar *m_storageUsageBar;
    
    // Data
    QVector<Recording> m_recordings;
    int m_selectedRecordingIndex;
};

#endif // RECORDEDVIDEOSWIDGET_H