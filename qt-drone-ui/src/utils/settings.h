#ifndef SETTINGS_H
#define SETTINGS_H

#include <QSettings>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QJsonObject>

class Settings : public QObject
{
    Q_OBJECT

public:
    static Settings* instance();
    
    // Connection settings
    QString voxlHost() const;
    int voxlPort() const;
    bool silMode() const;
    QString silHost() const;
    int silPort() const;
    int connectionTimeout() const;
    
    void setVoxlHost(const QString &host);
    void setVoxlPort(int port);
    void setSilMode(bool enabled);
    void setSilHost(const QString &host);
    void setSilPort(int port);
    void setConnectionTimeout(int timeout);
    
    // Camera settings
    QString cameraQuality() const;
    QString cameraFormat() const;
    int cameraFramerate() const;
    bool cameraFullscreen() const;
    
    void setCameraQuality(const QString &quality);
    void setCameraFormat(const QString &format);
    void setCameraFramerate(int framerate);
    void setCameraFullscreen(bool fullscreen);
    
    // Path planner settings
    QString coordinateSystem() const;
    int gridSize() const;
    bool showGrid() const;
    bool showAxes() const;
    
    void setCoordinateSystem(const QString &system);
    void setGridSize(int size);
    void setShowGrid(bool show);
    void setShowAxes(bool show);
    
    // UI settings
    bool darkTheme() const;
    QString language() const;
    QByteArray windowGeometry() const;
    QByteArray windowState() const;
    
    void setDarkTheme(bool dark);
    void setLanguage(const QString &language);
    void setWindowGeometry(const QByteArray &geometry);
    void setWindowState(const QByteArray &state);
    
    // File paths
    QString recordingsPath() const;
    QString pathsPath() const;
    QString logsPath() const;
    
    void setRecordingsPath(const QString &path);
    void setPathsPath(const QString &path);
    void setLogsPath(const QString &path);
    
    // Generic get/set
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);
    
    // Import/export
    void exportSettings(const QString &filePath);
    void importSettings(const QString &filePath);
    void resetToDefaults();
    
signals:
    void settingChanged(const QString &key, const QVariant &value);
    
private:
    explicit Settings(QObject *parent = nullptr);
    static Settings *s_instance;
    
    QSettings *m_settings;
    
    void loadDefaults();
};

#endif // SETTINGS_H