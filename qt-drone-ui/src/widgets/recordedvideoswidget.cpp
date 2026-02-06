#include "recordedvideoswidget.h"
#include "ui_recordedvideoswidget.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QUuid>
#include <QFile>
#include <QDir>
#include <QListWidgetItem>
#include <QDesktopServices>
#include <QUrl>
#include <QStorageInfo>

// Recording implementation
QJsonObject Recording::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["filePath"] = filePath;
    obj["fileSize"] = fileSize;
    obj["duration"] = duration;
    obj["createdAt"] = createdAt;
    obj["format"] = format;
    obj["quality"] = quality;
    return obj;
}

Recording Recording::fromJson(const QJsonObject &json)
{
    Recording recording;
    recording.id = json["id"].toString();
    recording.name = json["name"].toString();
    recording.filePath = json["filePath"].toString();
    recording.fileSize = json["fileSize"].toVariant().toLongLong();
    recording.duration = json["duration"].toVariant().toLongLong();
    recording.createdAt = json["createdAt"].toVariant().toLongLong();
    recording.format = json["format"].toString();
    recording.quality = json["quality"].toString();
    return recording;
}

// RecordedVideosWidget implementation
RecordedVideosWidget::RecordedVideosWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RecordedVideosWidget)
    , m_selectedRecordingIndex(-1)
{
    ui->setupUi(this);
    setupConnections();
    clearRecordingDetails();
    loadRecordings();
}

RecordedVideosWidget::~RecordedVideosWidget()
{
    saveRecordings();
    delete ui;
}

void RecordedVideosWidget::setupConnections()
{
    connect(ui->recordingList, &QListWidget::currentRowChanged, this, &RecordedVideosWidget::onRecordingSelectionChanged);
    connect(ui->playButton, &QPushButton::clicked, this, &RecordedVideosWidget::onPlayRecording);
    connect(ui->deleteButton, &QPushButton::clicked, this, &RecordedVideosWidget::onDeleteRecording);
    connect(ui->exportButton, &QPushButton::clicked, this, &RecordedVideosWidget::onExportRecording);
    connect(ui->importButton, &QPushButton::clicked, this, &RecordedVideosWidget::onImportRecording);
    connect(ui->refreshButton, &QPushButton::clicked, this, &RecordedVideosWidget::onRefreshRecordings);
}

void RecordedVideosWidget::addRecording(const QString &filePath, const QByteArray &data)
{
    Recording recording;
    recording.id = generateRecordingId();
    
    QFileInfo fileInfo(filePath);
    recording.name = fileInfo.baseName();
    recording.filePath = filePath;
    recording.fileSize = data.size();
    recording.duration = 0; // Would need video analysis to get actual duration
    recording.createdAt = QDateTime::currentMSecsSinceEpoch();
    recording.format = fileInfo.suffix().toLower();
    recording.quality = "high"; // Default quality
    
    // Save the actual recording data
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
    }
    
    m_recordings.append(recording);
    updateRecordingList();
    saveRecordings();
    
    // Select the newly added recording
    ui->recordingList->setCurrentRow(m_recordings.size() - 1);
}

void RecordedVideosWidget::loadRecordings()
{
    QString fileName = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recorded_videos.json";
    QFile file(fileName);
    
    if (!file.open(QIODevice::ReadOnly)) {
        return; // File doesn't exist or can't be opened
    }
    
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QJsonArray recordingsArray = obj["recordings"].toArray();
        
        m_recordings.clear();
        for (const QJsonValue &value : recordingsArray) {
            Recording recording = Recording::fromJson(value.toObject());
            // Verify file still exists
            if (QFile::exists(recording.filePath)) {
                // Update file size in case it changed
                QFileInfo fileInfo(recording.filePath);
                recording.fileSize = fileInfo.size();
                m_recordings.append(recording);
            }
        }
    }
    
    updateRecordingList();
}

void RecordedVideosWidget::saveRecordings()
{
    QString fileName = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recorded_videos.json";
    QDir().mkpath(QFileInfo(fileName).absolutePath());
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    
    QJsonObject obj;
    QJsonArray recordingsArray;
    
    for (const Recording &recording : m_recordings) {
        recordingsArray.append(recording.toJson());
    }
    
    obj["recordings"] = recordingsArray;
    obj["version"] = "1.0";
    obj["savedAt"] = QDateTime::currentMSecsSinceEpoch();
    
    QJsonDocument doc(obj);
    file.write(doc.toJson());
}

void RecordedVideosWidget::updateRecordingList()
{
    ui->recordingList->clear();
    
    for (int i = 0; i < m_recordings.size(); ++i) {
        const Recording &recording = m_recordings[i];
        QDateTime created = QDateTime::fromMSecsSinceEpoch(recording.createdAt);
        
        QString itemText = QString("%1\n%2 • %3 • %4")
                          .arg(recording.name)
                          .arg(formatFileSize(recording.fileSize))
                          .arg(recording.format.toUpper())
                          .arg(created.toString("MMM dd, yyyy hh:mm"));
        
        QListWidgetItem *item = new QListWidgetItem(itemText);
        item->setSizeHint(QSize(0, 50));
        ui->recordingList->addItem(item);
    }
    
    // Update button states
    bool hasSelection = m_selectedRecordingIndex >= 0 && m_selectedRecordingIndex < m_recordings.size();
    ui->playButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
    ui->exportButton->setEnabled(hasSelection);
    
    // Update storage info
    qint64 totalSize = 0;
    for (const Recording &recording : m_recordings) {
        totalSize += recording.fileSize;
    }
    
    // Update storage label and progress bar
    qint64 maxStorage = 10LL * 1024 * 1024 * 1024; // 10 GB
    ui->storageLabel->setText(QString("%1 used of %2 total (%3 recordings)")
                             .arg(formatFileSize(totalSize))
                             .arg(formatFileSize(maxStorage))
                             .arg(m_recordings.size()));
    
    int usagePercent = static_cast<int>((totalSize * 100) / maxStorage);
    ui->storageProgressBar->setValue(qMin(usagePercent, 100));
    ui->storageProgressBar->setFormat(QString("%1%").arg(usagePercent));
}

void RecordedVideosWidget::updateRecordingDetails()
{
    Recording *recording = getSelectedRecording();
    if (!recording) {
        clearRecordingDetails();
        return;
    }
    
    ui->recordingNameLabel->setText(recording->name);
    
    QDateTime created = QDateTime::fromMSecsSinceEpoch(recording->createdAt);
    ui->recordingDateLabel->setText("Created: " + created.toString("MMM dd, yyyy hh:mm:ss"));
    
    ui->recordingPathLabel->setText("Path: " + recording->filePath);
    ui->recordingFileSizeLabel->setText("Size: " + formatFileSize(recording->fileSize));
    ui->recordingDurationLabel->setText("Duration: " + formatDuration(recording->duration));
    ui->recordingFormatLabel->setText("Format: " + recording->format.toUpper());
    ui->recordingQualityLabel->setText("Quality: " + recording->quality);
}

void RecordedVideosWidget::clearRecordingDetails()
{
    ui->recordingNameLabel->setText("No recording selected");
    ui->recordingDateLabel->clear();
    ui->recordingPathLabel->clear();
    ui->recordingFileSizeLabel->clear();
    ui->recordingDurationLabel->clear();
    ui->recordingFormatLabel->clear();
    ui->recordingQualityLabel->clear();
}

Recording* RecordedVideosWidget::getSelectedRecording()
{
    if (m_selectedRecordingIndex >= 0 && m_selectedRecordingIndex < m_recordings.size()) {
        return &m_recordings[m_selectedRecordingIndex];
    }
    return nullptr;
}

QString RecordedVideosWidget::generateRecordingId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString RecordedVideosWidget::formatFileSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    } else {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
}

QString RecordedVideosWidget::formatDuration(qint64 seconds)
{
    if (seconds == 0) {
        return "Unknown";
    }
    
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    
    if (hours > 0) {
        return QString("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    } else {
        return QString("%1:%2").arg(minutes).arg(secs, 2, 10, QChar('0'));
    }
}

void RecordedVideosWidget::onRecordingSelectionChanged()
{
    m_selectedRecordingIndex = ui->recordingList->currentRow();
    updateRecordingDetails();
    updateRecordingList(); // Update button states
}

void RecordedVideosWidget::onPlayRecording()
{
    Recording *recording = getSelectedRecording();
    if (recording) {
        emit recordingPlayRequested(recording->filePath);
        
        // Also try to open with default system player
        QDesktopServices::openUrl(QUrl::fromLocalFile(recording->filePath));
    }
}

void RecordedVideosWidget::onDeleteRecording()
{
    Recording *recording = getSelectedRecording();
    if (!recording) return;
    
    int ret = QMessageBox::question(this, "Delete Recording", 
                                   QString("Are you sure you want to delete the recording '%1'?\n\nThis will also delete the video file from disk.").arg(recording->name),
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        QString recordingId = recording->id;
        QString filePath = recording->filePath;
        
        // Delete the file
        QFile::remove(filePath);
        
        // Remove from list
        m_recordings.removeAt(m_selectedRecordingIndex);
        m_selectedRecordingIndex = -1;
        
        updateRecordingList();
        clearRecordingDetails();
        saveRecordings();
        
        emit recordingDeleted(recordingId);
    }
}

void RecordedVideosWidget::onExportRecording()
{
    Recording *recording = getSelectedRecording();
    if (!recording) return;
    
    QFileInfo sourceInfo(recording->filePath);
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Recording",
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/" + recording->name + "." + recording->format,
        QString("%1 Files (*.%2)").arg(recording->format.toUpper(), recording->format));
    
    if (!fileName.isEmpty()) {
        if (QFile::copy(recording->filePath, fileName)) {
            QMessageBox::information(this, "Export Successful", "Recording exported successfully.");
        } else {
            QMessageBox::warning(this, "Export Failed", "Failed to export recording.");
        }
    }
}

void RecordedVideosWidget::onImportRecording()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Import Recording",
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        "Video Files (*.mp4 *.avi *.mov *.mkv *.wmv)");
    
    if (!fileName.isEmpty()) {
        QFileInfo fileInfo(fileName);
        
        // Copy file to recordings directory
        QString recordingsDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/DroneRecordings";
        QDir().mkpath(recordingsDir);
        
        QString destPath = recordingsDir + "/" + fileInfo.fileName();
        
        if (QFile::copy(fileName, destPath)) {
            Recording recording;
            recording.id = generateRecordingId();
            recording.name = fileInfo.baseName();
            recording.filePath = destPath;
            recording.fileSize = fileInfo.size();
            recording.duration = 0; // Would need video analysis
            recording.createdAt = QDateTime::currentMSecsSinceEpoch();
            recording.format = fileInfo.suffix().toLower();
            recording.quality = "imported";
            
            m_recordings.append(recording);
            updateRecordingList();
            saveRecordings();
            
            // Select the imported recording
            ui->recordingList->setCurrentRow(m_recordings.size() - 1);
            
            QMessageBox::information(this, "Import Successful", "Recording imported successfully.");
        } else {
            QMessageBox::warning(this, "Import Failed", "Failed to import recording.");
        }
    }
}

void RecordedVideosWidget::onRefreshRecordings()
{
    loadRecordings();
    QMessageBox::information(this, "Refresh Complete", "Recording list has been refreshed.");
}
