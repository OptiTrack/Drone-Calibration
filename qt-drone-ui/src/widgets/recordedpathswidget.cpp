#include "recordedpathswidget.h"
#include "ui_recordedpathswidget.h"
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

// RecordedPathsWidget implementation
RecordedPathsWidget::RecordedPathsWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RecordedPathsWidget)
    , m_selectedPathIndex(-1)
{
    ui->setupUi(this);
    setupConnections();
    clearPathDetails();
    loadPaths();
}

RecordedPathsWidget::~RecordedPathsWidget()
{
    savePaths();
    delete ui;
}

void RecordedPathsWidget::setupConnections()
{
    connect(ui->pathList, &QListWidget::currentRowChanged, this, &RecordedPathsWidget::onPathSelectionChanged);
    connect(ui->loadButton, &QPushButton::clicked, this, &RecordedPathsWidget::onLoadPath);
    connect(ui->deleteButton, &QPushButton::clicked, this, &RecordedPathsWidget::onDeletePath);
    connect(ui->duplicateButton, &QPushButton::clicked, this, &RecordedPathsWidget::onDuplicatePath);
    connect(ui->importButton, &QPushButton::clicked, this, &RecordedPathsWidget::onImportPath);
    connect(ui->exportButton, &QPushButton::clicked, this, &RecordedPathsWidget::onExportPath);
    connect(ui->editPathButton, &QPushButton::clicked, this, &RecordedPathsWidget::onEditPath);
}

void RecordedPathsWidget::addPath(const QString &name, const QVector<QVector3D> &points)
{
    // Reload paths from disk to ensure we have the latest
    // (the path was already saved by PathPlannerWidget)
    loadPaths();
    
    // Select the newest path (should be the one just added)
    if (!m_paths.isEmpty()) {
        ui->pathList->setCurrentRow(m_paths.size() - 1);
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
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        return FlightPath();
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return FlightPath();
    }
    
    QJsonObject root = doc.object();
    
    // Try to load from JSON first (handles both new and legacy formats)
    FlightPath path = FlightPath::fromJson(root);
    
    // Override name from filename if not set in JSON
    if (path.name().isEmpty()) {
        QFileInfo fileInfo(filePath);
        QString baseName = fileInfo.baseName();
        // Remove timestamp suffix like _20231201_143022
        int underscorePos = baseName.lastIndexOf('_');
        if (underscorePos > 0) {
            QString possibleTime = baseName.mid(underscorePos + 1);
            if (possibleTime.length() == 6 && possibleTime.toInt() > 0) {
                int secondUnderscore = baseName.lastIndexOf('_', underscorePos - 1);
                if (secondUnderscore > 0) {
                    baseName = baseName.left(secondUnderscore);
                }
            }
        }
        baseName.replace('_', ' ');
        path.setName(baseName);
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
        if (path.waypointCount() > 0) {
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
    ui->pathList->blockSignals(true);
    ui->pathList->clear();
    
    for (int i = 0; i < m_paths.size(); ++i) {
        const FlightPath &path = m_paths[i];
        QDateTime created = path.createdAt();
        
        QString itemText = QString("%1\n%2 waypoints • %3")
                          .arg(path.name())
                          .arg(path.waypointCount())
                          .arg(created.toString("MMM dd, yyyy hh:mm"));
        
        QListWidgetItem *item = new QListWidgetItem(itemText);
        item->setSizeHint(QSize(0, 50));
        ui->pathList->addItem(item);
    }
    
    // Restore selection if valid
    if (m_selectedPathIndex >= 0 && m_selectedPathIndex < m_paths.size()) {
        ui->pathList->setCurrentRow(m_selectedPathIndex);
    }
    
    ui->pathList->blockSignals(false);
    
    // Update button states
    bool hasSelection = m_selectedPathIndex >= 0 && m_selectedPathIndex < m_paths.size();
    ui->loadButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
    ui->duplicateButton->setEnabled(hasSelection);
    ui->exportButton->setEnabled(hasSelection);
    ui->editPathButton->setEnabled(hasSelection);
}

void RecordedPathsWidget::updatePathDetails()
{
    FlightPath *path = getSelectedPath();
    if (!path) {
        clearPathDetails();
        return;
    }
    
    ui->pathNameLabel->setText(path->name());
    
    ui->pathCreatedLabel->setText("Created: " + path->createdAt().toString("MMM dd, yyyy hh:mm:ss"));
    
    ui->pathPointCountLabel->setText(QString("Waypoints: %1").arg(path->waypointCount()));
    
    // Calculate path length
    ui->pathLengthLabel->setText(QString("Length: %1 m").arg(path->totalDistance(), 0, 'f', 1));
    
    ui->pathDescriptionEdit->setPlainText(path->description());
    
    // Update waypoint details list
    ui->waypointDetailsList->clear();
    for (int i = 0; i < path->waypointCount(); ++i) {
        const Waypoint &wp = path->waypoint(i);
        QString text = QString("WP %1: (%2, %3, %4)")
                      .arg(i + 1)
                      .arg(wp.x(), 0, 'f', 1)
                      .arg(wp.y(), 0, 'f', 1)
                      .arg(wp.z(), 0, 'f', 1);
        ui->waypointDetailsList->addItem(text);
    }
}

void RecordedPathsWidget::clearPathDetails()
{
    ui->pathNameLabel->setText("No path selected");
    ui->pathCreatedLabel->clear();
    ui->pathPointCountLabel->clear();
    ui->pathLengthLabel->clear();
    ui->pathDescriptionEdit->clear();
    ui->waypointDetailsList->clear();
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
    m_selectedPathIndex = ui->pathList->currentRow();
    updatePathDetails();
    
    // Update button states
    bool hasSelection = m_selectedPathIndex >= 0 && m_selectedPathIndex < m_paths.size();
    ui->loadButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
    ui->duplicateButton->setEnabled(hasSelection);
    ui->exportButton->setEnabled(hasSelection);
    ui->editPathButton->setEnabled(hasSelection);
}

void RecordedPathsWidget::onLoadPath()
{
    FlightPath *path = getSelectedPath();
    if (path) {
        // Convert Waypoints to QVector3D for compatibility
        QVector<QVector3D> points;
        for (int i = 0; i < path->waypointCount(); ++i) {
            const Waypoint &wp = path->waypoint(i);
            points.append(QVector3D(wp.x(), wp.y(), wp.z()));
        }
        emit pathLoadRequested(points);
    }
}

void RecordedPathsWidget::onDeletePath()
{
    FlightPath *path = getSelectedPath();
    if (!path) return;
    
    int ret = QMessageBox::question(this, "Delete Path", 
                                   QString("Are you sure you want to delete the path '%1'?\n\nThis will permanently remove the file from disk.").arg(path->name()),
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        QString pathId = path->id();
        
        // Find and delete the JSON file
        QString pathsDir = getPathsDirectory();
        QString fileName = QString("%1.json").arg(path->name().replace(" ", "_"));
        QString filePath = QDir(pathsDir).filePath(fileName);
        
        if (QFile::exists(filePath)) {
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
    
    QString sanitizedName = path->name();
    sanitizedName.replace(" ", "_");
    
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Path",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + sanitizedName + ".json",
        "JSON Files (*.json)");
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(path->toJson());
            file.write(doc.toJson(QJsonDocument::Indented));
            file.close();
            QMessageBox::information(this, "Export Successful", 
                                    QString("Path exported successfully to:\n%1").arg(fileName));
        } else {
            QMessageBox::warning(this, "Export Failed", "Failed to export path.");
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
                ui->pathList->setCurrentRow(m_paths.size() - 1);
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
        QString newDescription = ui->pathDescriptionEdit->toPlainText();
        if (path->description() != newDescription) {
            path->setDescription(newDescription);
            
            // Save to file
            QString pathsDir = getPathsDirectory();
            QString fileName = QString("%1.json").arg(path->name().replace(" ", "_"));
            QString filePath = QDir(pathsDir).filePath(fileName);
            
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(QJsonDocument(path->toJson()).toJson(QJsonDocument::Indented));
                file.close();
            }
        }
        
        // Load the path into the Flight Planner for editing
        QVector<QVector3D> points;
        for (int i = 0; i < path->waypointCount(); ++i) {
            const Waypoint &wp = path->waypoint(i);
            points.append(QVector3D(wp.x(), wp.y(), wp.z()));
        }
        emit pathLoadRequested(points);
    }
}

void RecordedPathsWidget::onDuplicatePath()
{
    FlightPath *path = getSelectedPath();
    if (!path) return;
    
    // Create a copy with a new name
    FlightPath duplicate = *path;
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    duplicate.setName(path->name() + " (Copy)");
    
    // Save to file
    QString pathsDir = getPathsDirectory();
    QString fileName = QString("%1_%2.json").arg(duplicate.name().replace(" ", "_")).arg(timestamp);
    QString destPath = QDir(pathsDir).filePath(fileName);
    
    QFile file(destPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(duplicate.toJson()).toJson(QJsonDocument::Indented));
        file.close();
        
        // Reload paths to include the new file
        loadPaths();
        
        // Select the duplicated path (should be the newest)
        if (!m_paths.isEmpty()) {
            ui->pathList->setCurrentRow(m_paths.size() - 1);
        }
    } else {
        QMessageBox::warning(this, "Duplicate Failed", "Failed to duplicate path file.");
    }
}