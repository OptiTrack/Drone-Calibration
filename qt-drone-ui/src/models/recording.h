#ifndef RECORDING_H
#define RECORDING_H

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QFileInfo>
#include <QSize>

class Recording
{
public:
    enum RecordingType {
        VideoRecording,
        PhotoRecording,
        TelemetryRecording
    };
    
    enum RecordingQuality {
        LowQuality,
        MediumQuality,
        HighQuality,
        UltraQuality
    };
    
    Recording();
    Recording(const QString &filePath, RecordingType type = VideoRecording);
    
    // Getters
    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString filePath() const { return m_filePath; }
    QString fileName() const;
    QString fileExtension() const;
    RecordingType type() const { return m_type; }
    RecordingQuality quality() const { return m_quality; }
    qint64 fileSize() const { return m_fileSize; }
    qint64 duration() const { return m_duration; } // in milliseconds
    QSize resolution() const { return m_resolution; }
    int frameRate() const { return m_frameRate; }
    QString codec() const { return m_codec; }
    QDateTime createdAt() const { return m_createdAt; }
    QDateTime recordedAt() const { return m_recordedAt; }
    QString description() const { return m_description; }
    QStringList tags() const { return m_tags; }
    bool exists() const;
    
    // Setters
    void setName(const QString &name);
    void setFilePath(const QString &filePath);
    void setType(RecordingType type) { m_type = type; }
    void setQuality(RecordingQuality quality) { m_quality = quality; }
    void setDuration(qint64 duration) { m_duration = duration; }
    void setResolution(const QSize &resolution) { m_resolution = resolution; }
    void setFrameRate(int frameRate) { m_frameRate = frameRate; }
    void setCodec(const QString &codec) { m_codec = codec; }
    void setRecordedAt(const QDateTime &dateTime) { m_recordedAt = dateTime; }
    void setDescription(const QString &description) { m_description = description; }
    void setTags(const QStringList &tags) { m_tags = tags; }
    void addTag(const QString &tag);
    void removeTag(const QString &tag);
    
    // Utility functions
    void updateFileInfo();
    QString formatFileSize() const;
    QString formatDuration() const;
    QString qualityString() const;
    QString typeString() const;
    QJsonObject toJson() const;
    static Recording fromJson(const QJsonObject &json);
    
    // File operations
    bool deleteFile();
    bool moveFile(const QString &newPath);
    bool copyFile(const QString &destinationPath);
    
    // Thumbnail/preview
    QString thumbnailPath() const;
    bool hasThumbnail() const;
    bool generateThumbnail();
    
    // Operators
    bool operator==(const Recording &other) const;
    bool operator!=(const Recording &other) const;
    bool operator<(const Recording &other) const; // For sorting by date
    
private:
    QString generateNewId();
    void initializeFromFile();
    
    QString m_id;
    QString m_name;
    QString m_filePath;
    RecordingType m_type;
    RecordingQuality m_quality;
    qint64 m_fileSize;
    qint64 m_duration;
    QSize m_resolution;
    int m_frameRate;
    QString m_codec;
    QDateTime m_createdAt;
    QDateTime m_recordedAt;
    QString m_description;
    QStringList m_tags;
};

Q_DECLARE_METATYPE(Recording)

#endif // RECORDING_H