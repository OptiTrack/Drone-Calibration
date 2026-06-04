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
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QDesktopServices>
#include <QUrl>
#include <QSet>
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
    recording.createdAt = QDateTime::currentMSecsSinceEpoch();
    recording.format = fileInfo.suffix().toLower();
    recording.quality = "high";
    recording.duration = 0;

    // Save raw data only if provided (MP4 recordings are already on disk)
    if (!data.isEmpty()) {
        QDir().mkpath(fileInfo.absolutePath());
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly))
            file.write(data);
    }

    // Read actual file size from disk
    recording.fileSize = QFileInfo(filePath).size();

    // Don't add duplicates
    for (const Recording &r : std::as_const(m_recordings)) {
        if (r.filePath == filePath)
            return;
    }

    m_recordings.append(recording);
    updateRecordingList();
    saveRecordings();
    ui->recordingList->setCurrentRow(m_recordings.size() - 1);
}

void RecordedVideosWidget::loadRecordings()
{
    // Load persisted manifest
    const QString manifestPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                 + QStringLiteral("/recorded_videos.json");
    QFile file(manifestPath);
    QSet<QString> knownPaths;

    m_recordings.clear();

    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            const QJsonArray arr = doc.object()[QStringLiteral("recordings")].toArray();
            for (const QJsonValue &v : arr) {
                Recording r = Recording::fromJson(v.toObject());
                if (QFile::exists(r.filePath)) {
                    r.fileSize = QFileInfo(r.filePath).size();
                    m_recordings.append(r);
                    knownPaths.insert(r.filePath);
                }
            }
        }
    }

    // Also scan Videos/CaliDrone for any MP4 files not yet in the manifest
    const QString caliDroneDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                                 + QStringLiteral("/CaliDrone");
    const QDir dir(caliDroneDir);
    if (dir.exists()) {
        const QFileInfoList files = dir.entryInfoList(
            {QStringLiteral("*.mp4"), QStringLiteral("*.avi"), QStringLiteral("*.mov"),
             QStringLiteral("*.mkv"), QStringLiteral("*.mjpeg")},
            QDir::Files, QDir::Time);
        for (const QFileInfo &fi : files) {
            if (knownPaths.contains(fi.absoluteFilePath()))
                continue;
            Recording r;
            r.id = generateRecordingId();
            r.name = fi.baseName();
            r.filePath = fi.absoluteFilePath();
            r.fileSize = fi.size();
            r.duration = 0;
            r.createdAt = fi.lastModified().toMSecsSinceEpoch();
            r.format = fi.suffix().toLower();
            r.quality = QStringLiteral("high");
            m_recordings.append(r);
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
    // Block signals so clearing the list doesn't fire currentRowChanged and reset m_selectedRecordingIndex
    ui->recordingList->blockSignals(true);
    ui->recordingList->clear();

    for (int i = 0; i < m_recordings.size(); ++i) {
        const Recording &recording = m_recordings[i];
        QDateTime created = QDateTime::fromMSecsSinceEpoch(recording.createdAt);

        QString itemText = QString("%1\n%2 \u2022 %3 \u2022 %4")
                          .arg(recording.name)
                          .arg(formatFileSize(recording.fileSize))
                          .arg(recording.format.toUpper())
                          .arg(created.toString("MMM dd, yyyy hh:mm"));

        QListWidgetItem *item = new QListWidgetItem(itemText);
        item->setSizeHint(QSize(0, 50));
        ui->recordingList->addItem(item);
    }

    // Restore selection
    if (m_selectedRecordingIndex >= 0 && m_selectedRecordingIndex < m_recordings.size())
        ui->recordingList->setCurrentRow(m_selectedRecordingIndex);

    ui->recordingList->blockSignals(false);

    updateButtonStates();
    updateStorageInfo();
}

void RecordedVideosWidget::updateButtonStates()
{
    const bool hasSelection = m_selectedRecordingIndex >= 0
                              && m_selectedRecordingIndex < m_recordings.size();
    ui->playButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
    ui->exportButton->setEnabled(hasSelection);
}

void RecordedVideosWidget::updateStorageInfo()
{
    qint64 totalSize = 0;
    for (const Recording &recording : std::as_const(m_recordings))
        totalSize += recording.fileSize;

    const QString caliDroneDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                                 + QStringLiteral("/CaliDrone");
    const QStorageInfo storage(caliDroneDir);
    const qint64 available = storage.isValid() ? storage.bytesAvailable() : (50LL * 1024 * 1024 * 1024);
    const qint64 total     = storage.isValid() ? storage.bytesTotal()     : (50LL * 1024 * 1024 * 1024);
    const qint64 diskUsed  = total - available;

    ui->storageLabel->setText(
        QString("%1 recordings  \u2022  %2 recorded  \u2022  %3 free on disk")
        .arg(m_recordings.size())
        .arg(formatFileSize(totalSize))
        .arg(formatFileSize(available)));

    const int usagePercent = total > 0 ? static_cast<int>((diskUsed * 100) / total) : 0;
    ui->storageProgressBar->setValue(qMin(usagePercent, 100));
    ui->storageProgressBar->setFormat(QString("%1% disk used").arg(usagePercent));
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

    // Probe media for duration and thumbnail if not already known
    if (recording->duration == 0 || ui->thumbnailLabel->text() == QStringLiteral("No preview available"))
        probeRecordingMedia(recording);
}

void RecordedVideosWidget::clearRecordingDetails()
{
    ui->recordingNameLabel->setText("No recording selected");
    ui->recordingDateLabel->clear();
    ui->recordingPathLabel->clear();
    ui->recordingFileSizeLabel->clear();
    ui->recordingDurationLabel->clear();
    ui->recordingFormatLabel->clear();
    ui->recordingQualityLabel->clear();    ui->thumbnailLabel->setText(QStringLiteral("No preview available"));
    ui->thumbnailLabel->setPixmap(QPixmap());
}

void RecordedVideosWidget::probeRecordingMedia(Recording *recording)
{
    if (!recording) return;
    if (m_probedIds.contains(recording->id)) return;
    m_probedIds.insert(recording->id);

    const QString filePath = recording->filePath;
    const QString recId    = recording->id;

    auto *player = new QMediaPlayer(this);
    auto *sink   = new QVideoSink(this);
    player->setVideoSink(sink);
    player->setSource(QUrl::fromLocalFile(filePath));

    // Shared flag — prevents both lambdas from acting after the first valid frame.
    // QSharedPointer keeps it alive until the last capturing lambda is destroyed.
    auto probed = QSharedPointer<bool>::create(false);

    // Grab the first valid frame as thumbnail, then stop the player.
    connect(sink, &QVideoSink::videoFrameChanged, this,
            [this, probed, player, sink](const QVideoFrame &frame) {
        if (*probed || !frame.isValid()) return;
        *probed = true;
        sink->disconnect(this); // stop receiving further frames

        const QImage img = frame.toImage();
        if (!img.isNull()) {
            ui->thumbnailLabel->setPixmap(
                QPixmap::fromImage(img).scaled(ui->thumbnailLabel->size(),
                                               Qt::KeepAspectRatio, Qt::SmoothTransformation));
            ui->thumbnailLabel->setText(QString());
        }
        player->stop(); // StoppedState handler will deleteLater
    });

    // Read duration once. Do NOT call player->play() here — the player is already
    // playing (started below). Re-calling play() on every LoadedMedia/BufferedMedia
    // transition causes an infinite signal loop that exhausts CPU and crashes.
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this, recId, probed, player](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia || status == QMediaPlayer::NoMedia) {
            player->stop();
            return;
        }
        if (status != QMediaPlayer::LoadedMedia && status != QMediaPlayer::BufferedMedia)
            return;
        if (*probed) return; // frame already captured

        const qint64 durationMs = player->duration();
        if (durationMs > 0) {
            const qint64 durationSecs = durationMs / 1000;
            for (Recording &r : m_recordings) {
                if (r.id == recId) {
                    r.duration = durationSecs;
                    break;
                }
            }
            ui->recordingDurationLabel->setText(
                QStringLiteral("Duration: ") + formatDuration(durationSecs));
            saveRecordings();
        }
        // Seek to 10% for a non-black frame; the seek delivers a frame via
        // QVideoSink without needing another play() call.
        player->setPosition(qMax(0LL, durationMs / 10));
    });

    // Clean up only when fully stopped (not on pause)
    connect(player, &QMediaPlayer::playbackStateChanged, this,
            [player](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::StoppedState)
            player->deleteLater();
    });

    player->play();
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
    updateButtonStates();
    updateRecordingDetails();
}

void RecordedVideosWidget::onPlayRecording()
{
    Recording *recording = getSelectedRecording();
    if (recording)
        QDesktopServices::openUrl(QUrl::fromLocalFile(recording->filePath));
}

void RecordedVideosWidget::onDeleteRecording()
{
    Recording *recording = getSelectedRecording();
    if (!recording) return;

    const QString recordingId = recording->id;
    const QString filePath = recording->filePath;

    QFile::remove(filePath);
    m_recordings.removeAt(m_selectedRecordingIndex);
    m_selectedRecordingIndex = -1;

    updateRecordingList();
    clearRecordingDetails();
    saveRecordings();
    emit recordingDeleted(recordingId);
}

void RecordedVideosWidget::onExportRecording()
{
    Recording *recording = getSelectedRecording();
    if (!recording) return;

    const QString dest = QFileDialog::getSaveFileName(this,
        QStringLiteral("Export Recording"),
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
            + QStringLiteral("/") + recording->name + QStringLiteral(".") + recording->format,
        QString("%1 Files (*.%2)").arg(recording->format.toUpper(), recording->format));

    if (!dest.isEmpty())
        QFile::copy(recording->filePath, dest);
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
}
