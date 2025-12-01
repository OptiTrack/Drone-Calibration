#include "recordedpathswidget.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QUuid>
#include <QFile>
#include <QTextStream>
#include <QListWidgetItem>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>

// FlightPath implementation
QJsonObject FlightPath::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["createdAt"] = createdAt;
    obj["description"] = description;
    
    QJsonArray pointsArray;
    for (const QVector3D &point : points) {
        QJsonObject pointObj;
        pointObj["x"] = point.x();
        pointObj["y"] = point.y();
        pointObj["z"] = point.z();
        pointsArray.append(pointObj);
    }
    obj["points"] = pointsArray;
    
    return obj;
}

FlightPath FlightPath::fromJson(const QJsonObject &json)
{
    FlightPath path;
    path.id = json["id"].toString();
    path.name = json["name"].toString();
    path.createdAt = json["createdAt"].toVariant().toLongLong();
    path.description = json["description"].toString();
    
    QJsonArray pointsArray = json["points"].toArray();
    for (const QJsonValue &value : pointsArray) {
        QJsonObject pointObj = value.toObject();
        QVector3D point(
            pointObj["x"].toDouble(),
            pointObj["y"].toDouble(),
            pointObj["z"].toDouble()
        );
        path.points.append(point);
    }
    
    return path;
}

// RecordedPathsWidget implementation
RecordedPathsWidget::RecordedPathsWidget(QWidget *parent)
    : QWidget(parent)
    , ui(nullptr)
    , m_mainLayout(nullptr)
    , m_contentLayout(nullptr)
    , m_pathListGroup(nullptr)
    , m_pathListLayout(nullptr)
    , m_pathList(nullptr)
    , m_pathButtonsLayout(nullptr)
    , m_deleteButton(nullptr)
    , m_loadButton(nullptr)
    , m_exportButton(nullptr)
    , m_importButton(nullptr)
    , m_duplicateButton(nullptr)
    , m_pathDetailsGroup(nullptr)
    , m_pathDetailsLayout(nullptr)
    , m_pathNameLabel(nullptr)
    , m_pathCreatedLabel(nullptr)
    , m_pathPointCountLabel(nullptr)
    , m_pathLengthLabel(nullptr)
    , m_pathDescriptionEdit(nullptr)
    , m_editPathButton(nullptr)
    , m_waypointDetailsList(nullptr)
    , m_selectedPathIndex(-1)
{
    setupUI();
    loadPaths();
}

RecordedPathsWidget::~RecordedPathsWidget()
{
    savePaths();
}

void RecordedPathsWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);
    
    // Create content layout
    m_contentLayout = new QHBoxLayout;
    m_mainLayout->addLayout(m_contentLayout);
    
    // Create path list group
    m_pathListGroup = new QGroupBox("Recorded Paths");
    m_pathListGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    m_pathListGroup->setMinimumWidth(300);
    m_contentLayout->addWidget(m_pathListGroup);
    
    m_pathListLayout = new QVBoxLayout(m_pathListGroup);
    
    // Path list
    m_pathList = new QListWidget;
    m_pathList->setStyleSheet(
        "QListWidget { background-color: #1f2937; color: white; border: 1px solid #4b5563; } "
        "QListWidget::item { padding: 8px; border-bottom: 1px solid #374151; } "
        "QListWidget::item:hover { background-color: #374151; } "
        "QListWidget::item:selected { background-color: #3b82f6; }"
    );
    m_pathListLayout->addWidget(m_pathList);
    
    // Path buttons
    m_pathButtonsLayout = new QHBoxLayout;
    
    m_loadButton = new QPushButton("Load");
    m_loadButton->setStyleSheet(
        "QPushButton { background-color: #059669; color: white; border: none; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #047857; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    
    m_deleteButton = new QPushButton("Delete");
    m_deleteButton->setStyleSheet(
        "QPushButton { background-color: #dc2626; color: white; border: none; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #b91c1c; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    
    m_duplicateButton = new QPushButton("Duplicate");
    m_duplicateButton->setStyleSheet(
        "QPushButton { background-color: #374151; color: white; border: 1px solid #4b5563; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #4b5563; } "
        "QPushButton:disabled { background-color: #1f2937; }"
    );
    
    m_pathButtonsLayout->addWidget(m_loadButton);
    m_pathButtonsLayout->addWidget(m_deleteButton);
    m_pathButtonsLayout->addWidget(m_duplicateButton);
    m_pathListLayout->addLayout(m_pathButtonsLayout);
    
    // Import/Export buttons
    QHBoxLayout *importExportLayout = new QHBoxLayout;
    
    m_importButton = new QPushButton("Import");
    m_importButton->setStyleSheet(
        "QPushButton { background-color: #374151; color: white; border: 1px solid #4b5563; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #4b5563; }"
    );
    
    m_exportButton = new QPushButton("Export");
    m_exportButton->setStyleSheet(
        "QPushButton { background-color: #374151; color: white; border: 1px solid #4b5563; padding: 6px 12px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #4b5563; } "
        "QPushButton:disabled { background-color: #1f2937; }"
    );
    
    importExportLayout->addWidget(m_importButton);
    importExportLayout->addWidget(m_exportButton);
    m_pathListLayout->addLayout(importExportLayout);
    
    // Create path details group
    m_pathDetailsGroup = new QGroupBox("Path Details");
    m_pathDetailsGroup->setStyleSheet(
        "QGroupBox { color: white; border: 1px solid #4b5563; border-radius: 4px; margin-top: 1ex; padding-top: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    m_contentLayout->addWidget(m_pathDetailsGroup, 1);
    
    m_pathDetailsLayout = new QVBoxLayout(m_pathDetailsGroup);
    
    // Path info labels
    m_pathNameLabel = new QLabel("No path selected");
    m_pathNameLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: white; }");
    m_pathDetailsLayout->addWidget(m_pathNameLabel);
    
    m_pathCreatedLabel = new QLabel();
    m_pathCreatedLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_pathDetailsLayout->addWidget(m_pathCreatedLabel);
    
    m_pathPointCountLabel = new QLabel();
    m_pathPointCountLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_pathDetailsLayout->addWidget(m_pathPointCountLabel);
    
    m_pathLengthLabel = new QLabel();
    m_pathLengthLabel->setStyleSheet("QLabel { color: #9ca3af; }");
    m_pathDetailsLayout->addWidget(m_pathLengthLabel);
    
    // Description
    m_pathDetailsLayout->addWidget(new QLabel("Description:"));
    m_pathDescriptionEdit = new QTextEdit;
    m_pathDescriptionEdit->setMaximumHeight(100);
    m_pathDescriptionEdit->setStyleSheet(
        "QTextEdit { background-color: #1f2937; color: white; border: 1px solid #4b5563; border-radius: 4px; padding: 4px; }"
    );
    m_pathDetailsLayout->addWidget(m_pathDescriptionEdit);
    
    // Edit button
    m_editPathButton = new QPushButton("Edit Path");
    m_editPathButton->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; border: none; padding: 8px 16px; border-radius: 4px; } "
        "QPushButton:hover { background-color: #2563eb; } "
        "QPushButton:disabled { background-color: #374151; }"
    );
    m_pathDetailsLayout->addWidget(m_editPathButton);
    
    // Waypoint details list
    m_pathDetailsLayout->addWidget(new QLabel("Waypoints:"));
    m_waypointDetailsList = new QListWidget;
    m_waypointDetailsList->setStyleSheet(
        "QListWidget { background-color: #1f2937; color: white; border: 1px solid #4b5563; } "
        "QListWidget::item { padding: 4px; border-bottom: 1px solid #374151; } "
        "QListWidget::item:hover { background-color: #374151; }"
    );
    m_pathDetailsLayout->addWidget(m_waypointDetailsList);
    
    // Connect signals
    connect(m_pathList, &QListWidget::currentRowChanged, this, &RecordedPathsWidget::onPathSelectionChanged);
    connect(m_loadButton, &QPushButton::clicked, this, &RecordedPathsWidget::onLoadPath);
    connect(m_deleteButton, &QPushButton::clicked, this, &RecordedPathsWidget::onDeletePath);
    connect(m_duplicateButton, &QPushButton::clicked, this, &RecordedPathsWidget::onDuplicatePath);
    connect(m_importButton, &QPushButton::clicked, this, &RecordedPathsWidget::onImportPath);
    connect(m_exportButton, &QPushButton::clicked, this, &RecordedPathsWidget::onExportPath);
    connect(m_editPathButton, &QPushButton::clicked, this, &RecordedPathsWidget::onEditPath);
    
    // Initial state
    clearPathDetails();
}

void RecordedPathsWidget::addPath(const QString &name, const QVector<QVector3D> &points)
{
    // Reload paths from disk to ensure we have the latest
    // (the path was already saved by PathPlannerWidget)
    loadPaths();
    
    // Select the newest path (should be the one just added)
    if (!m_paths.isEmpty()) {
        m_pathList->setCurrentRow(m_paths.size() - 1);
    }
}

QString RecordedPathsWidget::getPathsDirectory()
{
    if (!m_pathsDirectory.isEmpty() && QDir(m_pathsDirectory).exists()) {
        return m_pathsDirectory;
    }
    
    QString appDir = QCoreApplication::applicationDirPath();
    
    // First priority: Use the source directory paths folder (defined by CMake at compile time)
    // This works for development - the SOURCE_DIR macro points to qt-drone-ui folder
#ifdef SOURCE_DIR
    QString sourcePathsDir = QString(SOURCE_DIR) + "/paths";
    QDir sourceDir(sourcePathsDir);
    if (sourceDir.exists()) {
        m_pathsDirectory = sourceDir.absolutePath();
        qDebug() << "Using source paths directory:" << m_pathsDirectory;
        return m_pathsDirectory;
    }
#endif
    
    // Second priority: paths folder next to the executable (for distribution)
    QString exePathsDir = appDir + "/paths";
    QDir exeDir(exePathsDir);
    if (exeDir.exists()) {
        m_pathsDirectory = exeDir.absolutePath();
        qDebug() << "Using exe-relative paths directory:" << m_pathsDirectory;
        return m_pathsDirectory;
    }
    
    // If neither exists, create the source directory one (for development)
#ifdef SOURCE_DIR
    QString newPathsDir = QString(SOURCE_DIR) + "/paths";
    QDir().mkpath(newPathsDir);
    m_pathsDirectory = QDir(newPathsDir).absolutePath();
#else
    // Fallback: create next to exe
    QDir().mkpath(exePathsDir);
    m_pathsDirectory = QDir(exePathsDir).absolutePath();
#endif
    
    qDebug() << "Created paths directory:" << m_pathsDirectory;
    return m_pathsDirectory;
}

FlightPath RecordedPathsWidget::loadPathFromFile(const QString &filePath)
{
    FlightPath path;
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        return path;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return path;
    }
    
    QJsonObject root = doc.object();
    
    // Store the file path
    path.filePath = filePath;
    
    // Get file info for metadata
    QFileInfo fileInfo(filePath);
    path.createdAt = fileInfo.birthTime().toMSecsSinceEpoch();
    if (path.createdAt == 0) {
        path.createdAt = fileInfo.lastModified().toMSecsSinceEpoch();
    }
    
    // Extract name from filename (remove extension and timestamp suffix)
    QString baseName = fileInfo.baseName();
    // Remove timestamp suffix like _20231201_143022
    int underscorePos = baseName.lastIndexOf('_');
    if (underscorePos > 0) {
        QString possibleTime = baseName.mid(underscorePos + 1);
        // Check if it looks like a timestamp (6 digits)
        if (possibleTime.length() == 6 && possibleTime.toInt() > 0) {
            int secondUnderscore = baseName.lastIndexOf('_', underscorePos - 1);
            if (secondUnderscore > 0) {
                baseName = baseName.left(secondUnderscore);
            }
        }
    }
    baseName.replace('_', ' ');
    path.name = baseName;
    
    // Generate unique ID from filename
    path.id = fileInfo.fileName();
    
    // Try to get description from JSON if present
    path.description = root["description"].toString();
    
    // Load waypoints - support both formats
    if (root.contains("waypoints")) {
        // New format with waypoints array (has id, x, y, z)
        QJsonArray waypointsArray = root["waypoints"].toArray();
        for (const QJsonValue &value : waypointsArray) {
            QJsonObject wpObj = value.toObject();
            QVector3D point(
                wpObj["x"].toDouble(),
                wpObj["y"].toDouble(),
                wpObj["z"].toDouble()
            );
            path.points.append(point);
        }
    } else if (root.contains("points")) {
        // Legacy format with points array
        QJsonArray pointsArray = root["points"].toArray();
        for (const QJsonValue &value : pointsArray) {
            QJsonObject pointObj = value.toObject();
            QVector3D point(
                pointObj["x"].toDouble(),
                pointObj["y"].toDouble(),
                pointObj["z"].toDouble()
            );
            path.points.append(point);
        }
    }
    
    return path;
}

void RecordedPathsWidget::loadPaths()
{
    m_paths.clear();
    
    QString pathsDir = getPathsDirectory();
    QDir dir(pathsDir);
    
    if (!dir.exists()) {
        updatePathList();
        return;
    }
    
    // Get all JSON files in the paths directory
    QStringList filters;
    filters << "*.json";
    dir.setNameFilters(filters);
    dir.setSorting(QDir::Time | QDir::Reversed);  // Oldest first, newest last
    
    QFileInfoList fileList = dir.entryInfoList(QDir::Files);
    
    for (const QFileInfo &fileInfo : fileList) {
        FlightPath path = loadPathFromFile(fileInfo.absoluteFilePath());
        if (!path.points.isEmpty()) {
            m_paths.append(path);
        }
    }
    
    updatePathList();
}

void RecordedPathsWidget::savePaths()
{
    // Paths are now saved as individual files in the paths folder
    // This function is kept for compatibility but individual file operations
    // are handled in addPath() and onDeletePath()
}

void RecordedPathsWidget::updatePathList()
{
    // Block signals to prevent currentRowChanged from firing during rebuild
    m_pathList->blockSignals(true);
    m_pathList->clear();
    
    for (int i = 0; i < m_paths.size(); ++i) {
        const FlightPath &path = m_paths[i];
        QDateTime created = QDateTime::fromMSecsSinceEpoch(path.createdAt);
        
        QString itemText = QString("%1\n%2 waypoints • %3")
                          .arg(path.name)
                          .arg(path.points.size())
                          .arg(created.toString("MMM dd, yyyy hh:mm"));
        
        QListWidgetItem *item = new QListWidgetItem(itemText);
        item->setSizeHint(QSize(0, 50));
        m_pathList->addItem(item);
    }
    
    // Restore selection if valid
    if (m_selectedPathIndex >= 0 && m_selectedPathIndex < m_paths.size()) {
        m_pathList->setCurrentRow(m_selectedPathIndex);
    }
    
    m_pathList->blockSignals(false);
    
    // Update button states
    bool hasSelection = m_selectedPathIndex >= 0 && m_selectedPathIndex < m_paths.size();
    m_loadButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
    m_duplicateButton->setEnabled(hasSelection);
    m_exportButton->setEnabled(hasSelection);
    m_editPathButton->setEnabled(hasSelection);
}

void RecordedPathsWidget::updatePathDetails()
{
    FlightPath *path = getSelectedPath();
    if (!path) {
        clearPathDetails();
        return;
    }
    
    m_pathNameLabel->setText(path->name);
    
    QDateTime created = QDateTime::fromMSecsSinceEpoch(path->createdAt);
    m_pathCreatedLabel->setText("Created: " + created.toString("MMM dd, yyyy hh:mm:ss"));
    
    m_pathPointCountLabel->setText(QString("Waypoints: %1").arg(path->points.size()));
    
    // Calculate path length
    float totalLength = 0.0f;
    for (int i = 0; i < path->points.size() - 1; ++i) {
        totalLength += path->points[i].distanceToPoint(path->points[i + 1]);
    }
    m_pathLengthLabel->setText(QString("Length: %1 m").arg(totalLength, 0, 'f', 1));
    
    m_pathDescriptionEdit->setPlainText(path->description);
    
    // Update waypoint details list
    m_waypointDetailsList->clear();
    for (int i = 0; i < path->points.size(); ++i) {
        const QVector3D &wp = path->points[i];
        QString text = QString("WP %1: (%2, %3, %4)")
                      .arg(i + 1)
                      .arg(wp.x(), 0, 'f', 1)
                      .arg(wp.y(), 0, 'f', 1)
                      .arg(wp.z(), 0, 'f', 1);
        m_waypointDetailsList->addItem(text);
    }
}

void RecordedPathsWidget::clearPathDetails()
{
    m_pathNameLabel->setText("No path selected");
    m_pathCreatedLabel->clear();
    m_pathPointCountLabel->clear();
    m_pathLengthLabel->clear();
    m_pathDescriptionEdit->clear();
    m_waypointDetailsList->clear();
}

FlightPath* RecordedPathsWidget::getSelectedPath()
{
    if (m_selectedPathIndex >= 0 && m_selectedPathIndex < m_paths.size()) {
        return &m_paths[m_selectedPathIndex];
    }
    return nullptr;
}

QString RecordedPathsWidget::generatePathId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void RecordedPathsWidget::onPathSelectionChanged()
{
    m_selectedPathIndex = m_pathList->currentRow();
    updatePathDetails();
    
    // Update button states
    bool hasSelection = m_selectedPathIndex >= 0 && m_selectedPathIndex < m_paths.size();
    m_loadButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
    m_duplicateButton->setEnabled(hasSelection);
    m_exportButton->setEnabled(hasSelection);
    m_editPathButton->setEnabled(hasSelection);
}

void RecordedPathsWidget::onLoadPath()
{
    FlightPath *path = getSelectedPath();
    if (path) {
        emit pathLoadRequested(path->points);
    }
}

void RecordedPathsWidget::onDeletePath()
{
    FlightPath *path = getSelectedPath();
    if (!path) return;
    
    int ret = QMessageBox::question(this, "Delete Path", 
                                   QString("Are you sure you want to delete the path '%1'?\n\nThis will permanently remove the file from disk.").arg(path->name),
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        QString pathId = path->id;
        QString filePath = path->filePath;
        
        // Delete the file from disk
        if (!filePath.isEmpty() && QFile::exists(filePath)) {
            if (!QFile::remove(filePath)) {
                QMessageBox::warning(this, "Delete Failed", 
                                    QString("Failed to delete file:\n%1").arg(filePath));
                return;
            }
        }
        
        m_paths.removeAt(m_selectedPathIndex);
        m_selectedPathIndex = -1;
        
        updatePathList();
        clearPathDetails();
        
        emit pathDeleted(pathId);
    }
}

void RecordedPathsWidget::onExportPath()
{
    FlightPath *path = getSelectedPath();
    if (!path) return;
    
    QString sanitizedName = path->name;
    sanitizedName.replace(" ", "_");
    
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Path",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + sanitizedName + ".json",
        "JSON Files (*.json)");
    
    if (!fileName.isEmpty()) {
        // If the source file exists, just copy it
        if (!path->filePath.isEmpty() && QFile::exists(path->filePath)) {
            // Remove destination if it exists
            if (QFile::exists(fileName)) {
                QFile::remove(fileName);
            }
            if (QFile::copy(path->filePath, fileName)) {
                QMessageBox::information(this, "Export Successful", 
                                        QString("Path exported successfully to:\n%1").arg(fileName));
            } else {
                QMessageBox::warning(this, "Export Failed", "Failed to export path.");
            }
        } else {
            // Fall back to creating a new file
            QFile file(fileName);
            if (file.open(QIODevice::WriteOnly)) {
                QJsonDocument doc(path->toJson());
                file.write(doc.toJson(QJsonDocument::Indented));
                file.close();
                QMessageBox::information(this, "Export Successful", "Path exported successfully.");
            } else {
                QMessageBox::warning(this, "Export Failed", "Failed to export path.");
            }
        }
    }
}

void RecordedPathsWidget::onImportPath()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Import Path",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "JSON Files (*.json)");
    
    if (!fileName.isEmpty()) {
        // Get destination path
        QString pathsDir = getPathsDirectory();
        QFileInfo sourceInfo(fileName);
        
        // Generate unique filename
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString destFileName = sourceInfo.baseName() + "_imported_" + timestamp + ".json";
        QString destPath = pathsDir + "/" + destFileName;
        
        // Copy the file to the paths folder
        if (QFile::copy(fileName, destPath)) {
            // Reload paths to include the new file
            loadPaths();
            
            // Select the imported path (should be the newest)
            if (!m_paths.isEmpty()) {
                m_pathList->setCurrentRow(m_paths.size() - 1);
            }
            
            QMessageBox::information(this, "Import Successful", 
                                    QString("Path imported successfully to:\n%1").arg(destPath));
        } else {
            QMessageBox::warning(this, "Import Failed", "Failed to copy path file to paths folder.");
        }
    }
}

void RecordedPathsWidget::onEditPath()
{
    FlightPath *path = getSelectedPath();
    if (path) {
        // Save the description if it was modified
        QString newDescription = m_pathDescriptionEdit->toPlainText();
        if (path->description != newDescription && !path->filePath.isEmpty()) {
            // Update the description in the file
            QFile file(path->filePath);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray data = file.readAll();
                file.close();
                
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (doc.isObject()) {
                    QJsonObject root = doc.object();
                    root["description"] = newDescription;
                    
                    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
                        file.close();
                        path->description = newDescription;
                    }
                }
            }
        }
        
        // Load the path into the Flight Planner for editing
        emit pathLoadRequested(path->points);
    }
}

void RecordedPathsWidget::onDuplicatePath()
{
    FlightPath *path = getSelectedPath();
    if (!path || path->filePath.isEmpty()) return;
    
    // Get destination path
    QString pathsDir = getPathsDirectory();
    QFileInfo sourceInfo(path->filePath);
    
    // Generate unique filename for the copy
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString destFileName = sourceInfo.baseName() + "_copy_" + timestamp + ".json";
    QString destPath = pathsDir + "/" + destFileName;
    
    // Copy the file
    if (QFile::copy(path->filePath, destPath)) {
        // Reload paths to include the new file
        loadPaths();
        
        // Select the duplicated path (should be the newest)
        if (!m_paths.isEmpty()) {
            m_pathList->setCurrentRow(m_paths.size() - 1);
        }
    } else {
        QMessageBox::warning(this, "Duplicate Failed", "Failed to duplicate path file.");
    }
}