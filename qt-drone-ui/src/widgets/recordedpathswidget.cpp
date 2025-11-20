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
    FlightPath path;
    path.id = generatePathId();
    path.name = name;
    path.points = points;
    path.createdAt = QDateTime::currentMSecsSinceEpoch();
    path.description = "";
    
    m_paths.append(path);
    updatePathList();
    savePaths();
    
    // Select the newly added path
    m_pathList->setCurrentRow(m_paths.size() - 1);
}

void RecordedPathsWidget::loadPaths()
{
    QString fileName = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recorded_paths.json";
    QFile file(fileName);
    
    if (!file.open(QIODevice::ReadOnly)) {
        return; // File doesn't exist or can't be opened
    }
    
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QJsonArray pathsArray = obj["paths"].toArray();
        
        m_paths.clear();
        for (const QJsonValue &value : pathsArray) {
            FlightPath path = FlightPath::fromJson(value.toObject());
            m_paths.append(path);
        }
    }
    
    updatePathList();
}

void RecordedPathsWidget::savePaths()
{
    QString fileName = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recorded_paths.json";
    QDir().mkpath(QFileInfo(fileName).absolutePath());
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    
    QJsonObject obj;
    QJsonArray pathsArray;
    
    for (const FlightPath &path : m_paths) {
        pathsArray.append(path.toJson());
    }
    
    obj["paths"] = pathsArray;
    obj["version"] = "1.0";
    obj["savedAt"] = QDateTime::currentMSecsSinceEpoch();
    
    QJsonDocument doc(obj);
    file.write(doc.toJson());
}

void RecordedPathsWidget::updatePathList()
{
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
    updatePathList(); // Update button states
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
                                   QString("Are you sure you want to delete the path '%1'?").arg(path->name),
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        QString pathId = path->id;
        m_paths.removeAt(m_selectedPathIndex);
        m_selectedPathIndex = -1;
        
        updatePathList();
        clearPathDetails();
        savePaths();
        
        emit pathDeleted(pathId);
    }
}

void RecordedPathsWidget::onExportPath()
{
    FlightPath *path = getSelectedPath();
    if (!path) return;
    
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Path",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + path->name + ".json",
        "JSON Files (*.json)");
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(path->toJson());
            file.write(doc.toJson());
            QMessageBox::information(this, "Export Successful", "Path exported successfully.");
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
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            
            if (doc.isObject()) {
                FlightPath path = FlightPath::fromJson(doc.object());
                path.id = generatePathId(); // Generate new ID
                
                m_paths.append(path);
                updatePathList();
                savePaths();
                
                // Select the imported path
                m_pathList->setCurrentRow(m_paths.size() - 1);
                
                QMessageBox::information(this, "Import Successful", "Path imported successfully.");
            } else {
                QMessageBox::warning(this, "Import Failed", "Invalid path file format.");
            }
        } else {
            QMessageBox::warning(this, "Import Failed", "Failed to read path file.");
        }
    }
}

void RecordedPathsWidget::onEditPath()
{
    // For now, just allow editing the description
    FlightPath *path = getSelectedPath();
    if (path) {
        path->description = m_pathDescriptionEdit->toPlainText();
        savePaths();
    }
}

void RecordedPathsWidget::onDuplicatePath()
{
    FlightPath *path = getSelectedPath();
    if (!path) return;
    
    FlightPath newPath = *path;
    newPath.id = generatePathId();
    newPath.name = path->name + " (Copy)";
    newPath.createdAt = QDateTime::currentMSecsSinceEpoch();
    
    m_paths.append(newPath);
    updatePathList();
    savePaths();
    
    // Select the duplicated path
    m_pathList->setCurrentRow(m_paths.size() - 1);
}