#include "recordedvideoswidget.h"
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
    , ui(nullptr)
    , m_mainLayout(nullptr)
    , m_contentLayout(nullptr)
    , m_recordingListGroup(nullptr)
    , m_recordingListLayout(nullptr)
    , m_recordingList(nullptr)
    , m_recordingButtonsLayout(nullptr)
    , m_playButton(nullptr)
    , m_deleteButton(nullptr)
    , m_exportButton(nullptr)
    , m_importButton(nullptr)
    , m_refreshButton(nullptr)
    , m_recordingDetailsGroup(nullptr)
    , m_recordingDetailsLayout(nullptr)
    , m_recordingNameLabel(nullptr)
    , m_recordingCreatedLabel(nullptr)
    , m_recordingFilePathLabel(nullptr)
    , m_recordingFileSizeLabel(nullptr)
    , m_recordingDurationLabel(nullptr)
    , m_recordingFormatLabel(nullptr)
    , m_recordingQualityLabel(nullptr)
    , m_storageGroup(nullptr)
    , m_storageLayout(nullptr)
    , m_totalRecordingsLabel(nullptr)
    , m_totalSizeLabel(nullptr)
    , m_storageUsageBar(nullptr)
    , m_selectedRecordingIndex(-1)
{
    setupUI();
    loadRecordings();
}

RecordedVideosWidget::~RecordedVideosWidget()
{
    saveRecordings();
}

void RecordedVideosWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);
    
    // Create content layout
    m_contentLayout = new QHBoxLayout;
    m_mainLayout->addLayout(m_contentLayout);
    
    // Create recording list group
    m_recordingListGroup = new QGroupBox("Recorded Videos");
    m_recordingListGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    m_recordingListGroup->setMinimumWidth(300);
    m_contentLayout->addWidget(m_recordingListGroup);
    
    m_recordingListLayout = new QVBoxLayout(m_recordingListGroup);
    
    // Recording list
    m_recordingList = new QListWidget;
    m_recordingList->setStyleSheet(
        "QListWidget { background-color: #1f2937; color: white; border: 1px solid #4b5563; } "
        "QListWidget::item { padding: 8px; border-bottom: 1px solid #374151; } "
        "QListWidget::item:hover { background-color: #374151; } "
        "QListWidget::item:selected { background-color: #3b82f6; }"
    );
    m_recordingListLayout->addWidget(m_recordingList);
    
    // Recording buttons
    m_recordingButtonsLayout = new QHBoxLayout;
    
    m_playButton = new QPushButton("▶️ Play");
    m_playButton->setStyleSheet(
        "QPushButton { background-color: #059669; color: white; border: none; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #047857; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    
    m_deleteButton = new QPushButton("🗑️ Delete");
    m_deleteButton->setStyleSheet(
        "QPushButton { background-color: #dc2626; color: white; border: none; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #b91c1c; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    
    m_recordingButtonsLayout->addWidget(m_playButton);
    m_recordingButtonsLayout->addWidget(m_deleteButton);
    m_recordingListLayout->addLayout(m_recordingButtonsLayout);
    
    // Import/Export/Refresh buttons
    QHBoxLayout *importExportLayout = new QHBoxLayout;
    
    m_importButton = new QPushButton("📥 Import");
    m_importButton->setStyleSheet(
        "QPushButton { background-color: #374151; color: white; border: 1px solid #4b5563; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #4b5563; }"
    );
    
    m_exportButton = new QPushButton("📤 Export");
    m_exportButton->setStyleSheet(
        "QPushButton { background-color: #374151; color: white; border: 1px solid #4b5563; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #4b5563; } "
        "QPushButton:disabled { background-color: #1f2937; }"
    );
    
    m_refreshButton = new QPushButton("🔄 Refresh");
    m_refreshButton->setStyleSheet(
        "QPushButton { background-color: #374151; color: white; border: 1px solid #4b5563; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #4b5563; }"
    );
    
    importExportLayout->addWidget(m_importButton);
    importExportLayout->addWidget(m_exportButton);
    importExportLayout->addWidget(m_refreshButton);
    m_recordingListLayout->addLayout(importExportLayout);
    
    // Create recording details group
    m_recordingDetailsGroup = new QGroupBox("Recording Details");
    m_recordingDetailsGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    m_contentLayout->addWidget(m_recordingDetailsGroup, 1);
    
    m_recordingDetailsLayout = new QVBoxLayout(m_recordingDetailsGroup);
    
    // Recording info labels
    m_recordingNameLabel = new QLabel("No recording selected");
    m_recordingNameLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: white; }");
    m_recordingDetailsLayout->addWidget(m_recordingNameLabel);
    
    m_recordingCreatedLabel = new QLabel();
    m_recordingCreatedLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_recordingDetailsLayout->addWidget(m_recordingCreatedLabel);
    
    m_recordingFilePathLabel = new QLabel();
    m_recordingFilePathLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_recordingFilePathLabel->setWordWrap(true);
    m_recordingDetailsLayout->addWidget(m_recordingFilePathLabel);
    
    m_recordingFileSizeLabel = new QLabel();
    m_recordingFileSizeLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_recordingDetailsLayout->addWidget(m_recordingFileSizeLabel);
    
    m_recordingDurationLabel = new QLabel();
    m_recordingDurationLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_recordingDetailsLayout->addWidget(m_recordingDurationLabel);
    
    m_recordingFormatLabel = new QLabel();
    m_recordingFormatLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_recordingDetailsLayout->addWidget(m_recordingFormatLabel);
    
    m_recordingQualityLabel = new QLabel();
    m_recordingQualityLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_recordingDetailsLayout->addWidget(m_recordingQualityLabel);
    
    m_recordingDetailsLayout->addStretch();
    
    // Create storage info group
    m_storageGroup = new QGroupBox("Storage Information");
    m_storageGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    m_mainLayout->addWidget(m_storageGroup);
    
    m_storageLayout = new QVBoxLayout(m_storageGroup);
    
    m_totalRecordingsLabel = new QLabel("Total Recordings: 0");
    m_totalRecordingsLabel->setStyleSheet("QLabel { color: white; }");
    m_storageLayout->addWidget(m_totalRecordingsLabel);
    
    m_totalSizeLabel = new QLabel("Total Size: 0 MB");
    m_totalSizeLabel->setStyleSheet("QLabel { color: white; }");
    m_storageLayout->addWidget(m_totalSizeLabel);
    
    QLabel *storageUsageLabel = new QLabel("Storage Usage:");
    storageUsageLabel->setStyleSheet("QLabel { color: white; }");
    m_storageLayout->addWidget(storageUsageLabel);
    
    m_storageUsageBar = new QProgressBar;
    m_storageUsageBar->setRange(0, 100);
    m_storageUsageBar->setStyleSheet(
        "QProgressBar { border: 1px solid #4b5563; border-radius: 4px; text-align: center; } "
        "QProgressBar::chunk { background-color: #3b82f6; border-radius: 3px; }"
    );
    m_storageLayout->addWidget(m_storageUsageBar);
    
    // Connect signals
    connect(m_recordingList, &QListWidget::currentRowChanged, this, &RecordedVideosWidget::onRecordingSelectionChanged);
    connect(m_playButton, &QPushButton::clicked, this, &RecordedVideosWidget::onPlayRecording);
    connect(m_deleteButton, &QPushButton::clicked, this, &RecordedVideosWidget::onDeleteRecording);
    connect(m_exportButton, &QPushButton::clicked, this, &RecordedVideosWidget::onExportRecording);
    connect(m_importButton, &QPushButton::clicked, this, &RecordedVideosWidget::onImportRecording);
    connect(m_refreshButton, &QPushButton::clicked, this, &RecordedVideosWidget::onRefreshRecordings);
    
    // Initial state
    clearRecordingDetails();
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
    m_recordingList->setCurrentRow(m_recordings.size() - 1);
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
    m_recordingList->clear();
    
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
        m_recordingList->addItem(item);
    }
    
    // Update button states
    bool hasSelection = m_selectedRecordingIndex >= 0 && m_selectedRecordingIndex < m_recordings.size();
    m_playButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
    m_exportButton->setEnabled(hasSelection);
    
    // Update storage info
    qint64 totalSize = 0;
    for (const Recording &recording : m_recordings) {
        totalSize += recording.fileSize;
    }
    
    m_totalRecordingsLabel->setText(QString("Total Recordings: %1").arg(m_recordings.size()));
    m_totalSizeLabel->setText(QString("Total Size: %1").arg(formatFileSize(totalSize)));
    
    // Update storage usage bar (simplified - shows percentage of 10GB)
    qint64 maxStorage = 10LL * 1024 * 1024 * 1024; // 10 GB
    int usagePercent = static_cast<int>((totalSize * 100) / maxStorage);
    m_storageUsageBar->setValue(qMin(usagePercent, 100));
    m_storageUsageBar->setFormat(QString("%1% of 10 GB used").arg(usagePercent));
}

void RecordedVideosWidget::updateRecordingDetails()
{
    Recording *recording = getSelectedRecording();
    if (!recording) {
        clearRecordingDetails();
        return;
    }
    
    m_recordingNameLabel->setText(recording->name);
    
    QDateTime created = QDateTime::fromMSecsSinceEpoch(recording->createdAt);
    m_recordingCreatedLabel->setText("Created: " + created.toString("MMM dd, yyyy hh:mm:ss"));
    
    m_recordingFilePathLabel->setText("Path: " + recording->filePath);
    m_recordingFileSizeLabel->setText("Size: " + formatFileSize(recording->fileSize));
    m_recordingDurationLabel->setText("Duration: " + formatDuration(recording->duration));
    m_recordingFormatLabel->setText("Format: " + recording->format.toUpper());
    m_recordingQualityLabel->setText("Quality: " + recording->quality);
}

void RecordedVideosWidget::clearRecordingDetails()
{
    m_recordingNameLabel->setText("No recording selected");
    m_recordingCreatedLabel->clear();
    m_recordingFilePathLabel->clear();
    m_recordingFileSizeLabel->clear();
    m_recordingDurationLabel->clear();
    m_recordingFormatLabel->clear();
    m_recordingQualityLabel->clear();
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
    m_selectedRecordingIndex = m_recordingList->currentRow();
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
            m_recordingList->setCurrentRow(m_recordings.size() - 1);
            
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