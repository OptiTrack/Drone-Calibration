#include "recordedpathswidget.h"
#include "ui_recordedpathswidget.h"
#include "../utils/volumemanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QStandardPaths>
#include <QDateTime>
#include <QFile>
#include <QListWidgetItem>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
#include <QFileDevice>
#include <QStyleFactory>
#include <QFontMetrics>
#include <QAction>
#include <QUuid>
#include <cmath>

namespace {

QString canonicalOrAbsolutePath(const QString &filePath)
{
    QFileInfo fi(filePath);
    const QString c = fi.canonicalFilePath();
    if (!c.isEmpty())
        return QDir::toNativeSeparators(c);
    return QDir::toNativeSeparators(fi.absoluteFilePath());
}

QString displayBaseNameFromFile(const QString &filePath)
{
    QString baseName = QFileInfo(filePath).baseName();
    static const QRegularExpression tsSuffix(QStringLiteral("_\\d{8}_\\d{6}$"));
    const QRegularExpressionMatch m = tsSuffix.match(baseName);
    if (m.hasMatch())
        baseName = baseName.left(m.capturedStart());
    return FlightPath::displayNameFromFileBase(baseName);
}

QJsonObject plannerJsonFromPath(const FlightPath &path)
{
    QJsonObject root = path.toJson();
    root[QStringLiteral("version")] = 1;
    return root;
}

QJsonObject readJsonObject(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

bool copyDirectoryRecursively(const QString &sourceDirPath, const QString &destDirPath)
{
    QDir sourceDir(sourceDirPath);
    if (!sourceDir.exists())
        return false;
    QDir().mkpath(destDirPath);

    const QFileInfoList entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
    for (const QFileInfo &entry : entries) {
        const QString destPath = QDir(destDirPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(), destPath))
                return false;
        } else {
            QFile::remove(destPath);
            if (!QFile::copy(entry.absoluteFilePath(), destPath))
                return false;
        }
    }
    return true;
}

bool deletePathJsonOnDisk(const QString &sourceFilePath, const QString &pathsDir, const QString &displayName)
{
    QStringList candidates;
    auto add = [&candidates](const QString &p) {
        if (p.isEmpty())
            return;
        const QString n = canonicalOrAbsolutePath(p);
        if (!candidates.contains(n))
            candidates.append(n);
    };

    add(sourceFilePath);
    add(QDir(pathsDir).filePath(FlightPath::fileBaseFromDisplayName(displayName) + QStringLiteral(".json")));
    if (!sourceFilePath.isEmpty())
        add(QDir(pathsDir).filePath(QFileInfo(sourceFilePath).fileName()));

    for (const QString &p : candidates) {
        if (!QFile::exists(p))
            continue;
        QFile f(p);
        f.setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser);
        if (!f.remove())
            return false;
        return true;
    }
    return true;
}

} // namespace

// RecordedPathsWidget implementation
RecordedPathsWidget::RecordedPathsWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RecordedPathsWidget)
    , m_volumeManager(nullptr)
    , m_roomButton(nullptr)
    , m_roomMenu(nullptr)
    , m_newRoomButton(nullptr)
    , m_renameRoomButton(nullptr)
    , m_deleteRoomButton(nullptr)
    , m_roomMapLabel(nullptr)
    , m_selectedPathIndex(-1)
{
    ui->setupUi(this);
    // Windows "Vista" style paints an inset frame inside selected list items; Fusion draws a flat fill.
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        ui->pathList->setStyle(fusion);
    ui->pathList->setTextElideMode(Qt::ElideNone);
    setupRoomControls();
    setupConnections();
    clearPathDetails();
    loadPaths();
}

RecordedPathsWidget::~RecordedPathsWidget()
{
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
    connect(m_newRoomButton, &QPushButton::clicked, this, &RecordedPathsWidget::onNewRoom);
    connect(m_renameRoomButton, &QPushButton::clicked, this, &RecordedPathsWidget::onRenameRoom);
    connect(m_deleteRoomButton, &QPushButton::clicked, this, &RecordedPathsWidget::onDeleteRoom);
}

void RecordedPathsWidget::setupRoomControls()
{
    QWidget *roomBar = new QWidget(this);
    QHBoxLayout *roomLayout = new QHBoxLayout(roomBar);
    roomLayout->setContentsMargins(0, 0, 0, 4);
    roomLayout->setSpacing(6);

    QLabel *roomLabel = new QLabel(QStringLiteral("Room:"), roomBar);
    roomLabel->setStyleSheet(QStringLiteral("QLabel { color: white; font-weight: bold; }"));

    m_roomButton = new QToolButton(roomBar);
    m_roomButton->setText(QStringLiteral("No Room Selected"));
    m_roomButton->setMinimumWidth(220);
    m_roomButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_roomButton->setStyleSheet(QStringLiteral(
        "QToolButton { background:#374151; color:#e5e7eb; border:1px solid #4b5563; padding:4px 6px; border-radius:3px; text-align:left; }"
        "QToolButton:hover { background:#4b5563; }"));

    m_roomMenu = new QMenu(m_roomButton);
    m_roomMenu->setStyleSheet(QStringLiteral(
        "QMenu { background-color:#2d2d2d; color:#e5e7eb; border:1px solid #4b5563; }"
        "QMenu::item { padding:5px 24px 5px 12px; }"
        "QMenu::item:selected { background-color:#374151; }"
        "QMenu::separator { height:1px; background:#4b5563; margin:4px 0; }"));
    connect(m_roomButton, &QToolButton::clicked, this, [this]() {
        if (!m_roomMenu || !m_roomButton)
            return;
        m_roomMenu->popup(m_roomButton->mapToGlobal(QPoint(0, m_roomButton->height())));
    });

    m_newRoomButton = new QPushButton(QStringLiteral("New Room"), roomBar);
    m_renameRoomButton = new QPushButton(QStringLiteral("Rename"), roomBar);
    m_deleteRoomButton = new QPushButton(QStringLiteral("Delete Room"), roomBar);

    const QString buttonStyle = QStringLiteral(
        "QPushButton { background:#374151; color:white; border:1px solid #4b5563; padding:5px 8px; border-radius:3px; }"
        "QPushButton:hover { background:#4b5563; }"
        "QPushButton:disabled { background:#1f2937; color:#6b7280; }");
    for (QPushButton *button : {m_newRoomButton, m_renameRoomButton, m_deleteRoomButton})
        button->setStyleSheet(buttonStyle);

    m_roomMapLabel = new QLabel(QStringLiteral("Map: no room selected"), roomBar);
    m_roomMapLabel->setStyleSheet(QStringLiteral("QLabel { color:#9ca3af; }"));

    roomLayout->addWidget(roomLabel);
    roomLayout->addWidget(m_roomButton);
    roomLayout->addWidget(m_newRoomButton);
    roomLayout->addWidget(m_renameRoomButton);
    roomLayout->addWidget(m_deleteRoomButton);
    roomLayout->addStretch();
    roomLayout->addWidget(m_roomMapLabel);

    ui->mainLayout->insertWidget(0, roomBar);
}

void RecordedPathsWidget::setVolumeManager(VolumeManager *volumeManager)
{
    if (m_volumeManager == volumeManager)
        return;

    if (m_volumeManager)
        disconnect(m_volumeManager, nullptr, this, nullptr);

    m_volumeManager = volumeManager;
    if (m_volumeManager) {
        connect(m_volumeManager, &VolumeManager::volumeListChanged, this, &RecordedPathsWidget::refreshRooms);
        connect(m_volumeManager, &VolumeManager::activeVolumeChanged, this, [this](const VolumeManager::VolumeInfo &) {
            refreshRooms();
            loadPaths();
        });
        connect(m_volumeManager, &VolumeManager::activeVolumeCleared, this, [this]() {
            refreshRooms();
            loadPaths();
        });
    }

    refreshRooms();
    loadPaths();
}

void RecordedPathsWidget::refreshRooms()
{
    if (!m_roomButton || !m_roomMenu)
        return;

    m_roomMenu->clear();
    QString activeRoomName = QStringLiteral("No Room Selected");
    QString activeId;

    if (m_volumeManager) {
        const QList<VolumeManager::VolumeInfo> rooms = m_volumeManager->volumes();
        activeId = m_volumeManager->activeVolumeId();
        for (const VolumeManager::VolumeInfo &room : rooms) {
            QAction *roomAction = m_roomMenu->addAction(room.name);
            roomAction->setCheckable(true);
            roomAction->setChecked(room.id == activeId);
            connect(roomAction, &QAction::triggered, this, [this, room]() {
                emit roomChangeRequested(room.id);
            });
            if (room.id == activeId)
                activeRoomName = room.name;
        }

        if (!rooms.isEmpty())
            m_roomMenu->addSeparator();
    }

    QAction *noRoomAction = m_roomMenu->addAction(QStringLiteral("No Room Selected"));
    noRoomAction->setCheckable(true);
    noRoomAction->setChecked(activeId.isEmpty());
    connect(noRoomAction, &QAction::triggered, this, [this]() {
        emit roomChangeRequested(QString());
    });

    m_roomMenu->addSeparator();
    QAction *newRoomAction = m_roomMenu->addAction(QStringLiteral("+ Room"));
    connect(newRoomAction, &QAction::triggered, this, &RecordedPathsWidget::onNewRoom);

    m_roomButton->setText(activeRoomName);
    m_roomButton->setToolTip(activeId.isEmpty()
                                 ? QStringLiteral("No room selected.")
                                 : QStringLiteral("Saved Paths room: %1").arg(activeRoomName));
    const bool hasActiveRoom = m_volumeManager && m_volumeManager->hasActiveVolume();
    m_renameRoomButton->setEnabled(hasActiveRoom);
    m_deleteRoomButton->setEnabled(hasActiveRoom);
    updateRoomSummary();
}

void RecordedPathsWidget::addPath(const QString &name, const QVector<QVector3D> &points)
{
    Q_UNUSED(points);
    loadPaths();

    const QString targetFile = FlightPath::fileBaseFromDisplayName(name) + QStringLiteral(".json");
    for (int i = 0; i < m_paths.size(); ++i) {
        const QString src = m_paths[i].sourceFilePath();
        if (!src.isEmpty() && QFileInfo(src).fileName() == targetFile) {
            ui->pathList->setCurrentRow(i);
            return;
        }
    }
    if (!m_paths.isEmpty())
        ui->pathList->setCurrentRow(m_paths.size() - 1);
}

QString RecordedPathsWidget::getPathsDirectory()
{
    if (m_volumeManager && m_volumeManager->hasActiveVolume()) {
        const QString roomPathsDir = m_volumeManager->activePathsDir();
        if (!roomPathsDir.isEmpty()) {
            QDir().mkpath(roomPathsDir);
            m_pathsDirectory = QDir(roomPathsDir).absolutePath();
            return m_pathsDirectory;
        }
    }
    if (m_volumeManager)
        return {};

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

QStringList RecordedPathsWidget::legacyPathsDirectories() const
{
    QStringList dirs;
    auto addDir = [&dirs](const QString &path) {
        if (path.isEmpty())
            return;
        const QString abs = QDir(path).absolutePath();
        if (QDir(abs).exists() && !dirs.contains(abs))
            dirs.append(abs);
    };

#ifdef SOURCE_DIR
    addDir(QString(SOURCE_DIR) + QStringLiteral("/paths"));
#endif
    addDir(QCoreApplication::applicationDirPath() + QStringLiteral("/paths"));

    if (m_volumeManager && m_volumeManager->hasActiveVolume())
        dirs.removeAll(QDir(m_volumeManager->activePathsDir()).absolutePath());

    return dirs;
}

bool RecordedPathsWidget::writeJsonPreservingPlannerFields(const QString &destPath, const FlightPath &path,
                                                           const QJsonObject &sourceRoot) const
{
    QJsonObject root = sourceRoot.isEmpty() ? plannerJsonFromPath(path) : sourceRoot;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("name")] = path.name();
    root[QStringLiteral("description")] = path.description();
    root[QStringLiteral("created_at")] = path.createdAt().toString(Qt::ISODate);
    root[QStringLiteral("modified_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (!root.contains(QStringLiteral("waypoints"))) {
        QJsonArray waypointsArray;
        for (const Waypoint &wp : path.waypoints())
            waypointsArray.append(wp.toJson());
        root[QStringLiteral("waypoints")] = waypointsArray;
    }
    if (m_volumeManager && m_volumeManager->hasActiveVolume()) {
        const VolumeManager::VolumeInfo room = m_volumeManager->activeVolume();
        root[QStringLiteral("room_id")] = room.id;
        root[QStringLiteral("room_name")] = room.name;
    }

    QFile file(destPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool RecordedPathsWidget::copyPathIntoCurrentRoom(const QString &sourcePath, QString *destPath)
{
    if (!m_volumeManager || !m_volumeManager->hasActiveVolume())
        return false;

    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return false;

    const QString pathsDir = getPathsDirectory();
    QDir().mkpath(pathsDir);

    QString baseName = sourceInfo.completeBaseName();
    QString candidate = baseName + QStringLiteral(".json");
    QString destination = QDir(pathsDir).filePath(candidate);
    if (QFile::exists(destination)) {
        const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        candidate = QStringLiteral("%1_imported_%2.json").arg(baseName, timestamp);
        destination = QDir(pathsDir).filePath(candidate);
    }

    QJsonObject root = readJsonObject(sourcePath);
    if (!root.isEmpty()) {
        root[QStringLiteral("version")] = 1;
        const VolumeManager::VolumeInfo room = m_volumeManager->activeVolume();
        root[QStringLiteral("room_id")] = room.id;
        root[QStringLiteral("room_name")] = room.name;

        const QString oldBundleName = root.value(QStringLiteral("mapper_map_bundle")).toString();
        if (!oldBundleName.trimmed().isEmpty()) {
            const QString oldBundlePath = sourceInfo.absoluteDir().filePath(oldBundleName);
            if (QDir(oldBundlePath).exists()) {
                const QString roomBundle = m_volumeManager->activeMapBundleDir();
                if (!QDir(roomBundle).exists()) {
                    QDir().mkpath(m_volumeManager->activeMapDir());
                    copyDirectoryRecursively(oldBundlePath, roomBundle);
                }
                root[QStringLiteral("mapper_map_bundle")] = QStringLiteral("../map/mapper_map");
            }
        }

        QFile out(destination);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    } else if (!QFile::copy(sourcePath, destination)) {
        return false;
    }

    if (destPath)
        *destPath = destination;
    return true;
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
    FlightPath path = FlightPath::fromJson(root);

    const QFileInfo fi(filePath);
    const QDateTime mtime = fi.lastModified();

    const QDateTime parsedCreated = QDateTime::fromString(root.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    const QDateTime parsedModified = QDateTime::fromString(root.value(QStringLiteral("modified_at")).toString(), Qt::ISODate);

    // Filename is the source of truth for the on-disk identity (JSON often carries default "New Flight Path").
    path.setName(displayBaseNameFromFile(filePath));
    path.setCreatedAt(parsedCreated.isValid() ? parsedCreated : mtime);
    path.setModifiedAt(parsedModified.isValid() ? parsedModified : mtime);
    path.setSourceFilePath(canonicalOrAbsolutePath(filePath));
    return path;
}

void RecordedPathsWidget::loadPaths()
{
    m_paths.clear();
    
    QString pathsDir = getPathsDirectory();
    if (pathsDir.isEmpty()) {
        updatePathList();
        updateRoomSummary();
        return;
    }
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
    updateRoomSummary();
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
        // Stylesheet padding (~20px) + two text lines; 50px was clipping the metadata row.
        const QFontMetrics fm(ui->pathList->font());
        const int itemH = fm.lineSpacing() * 2 + 28;
        item->setSizeHint(QSize(0, qMax(itemH, 64)));
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

    updateRoomSummary();
    
    ui->pathDescriptionEdit->setPlainText(path->description());
    
    ui->waypointDetailsList->clear();
    for (int i = 0; i < path->waypointCount(); ++i) {
        const Waypoint &wp = path->waypoint(i);
        const int n = i + 1;

        const bool hasGps = std::abs(wp.latitude()) > 1e-9 || std::abs(wp.longitude()) > 1e-9;
        QString gps;
        if (hasGps) {
            gps = QStringLiteral("  lat %1  lon %2  rel_alt %3 m")
                      .arg(wp.latitude(), 0, 'f', 6)
                      .arg(wp.longitude(), 0, 'f', 6)
                      .arg(wp.relativeAltitudeM(), 0, 'f', 2);
        }

        QString labelNote;
        if (!wp.name().trimmed().isEmpty())
            labelNote = QStringLiteral("  label \"%1\"").arg(wp.name());

        const QString text = QStringLiteral("%1: (%2, %3, %4)  yaw %5°  hold %6 s  acc %7 m  %8  %9%10%11")
                                 .arg(n)
                                 .arg(wp.x(), 0, 'f', 2)
                                 .arg(wp.y(), 0, 'f', 2)
                                 .arg(wp.z(), 0, 'f', 2)
                                 .arg(wp.yawAngle(), 0, 'f', 1)
                                 .arg(wp.holdTime(), 0, 'f', 1)
                                 .arg(wp.acceptanceRadius(), 0, 'f', 2)
                                 .arg(wp.passThrough() ? QStringLiteral("pass") : QStringLiteral("stop"))
                                 .arg(wp.waypointType())
                                 .arg(gps)
                                 .arg(labelNote);
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
    updateRoomSummary();
}

FlightPath* RecordedPathsWidget::getSelectedPath()
{
    if (m_selectedPathIndex >= 0 && m_selectedPathIndex < m_paths.size()) {
        return &m_paths[m_selectedPathIndex];
    }
    return nullptr;
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
    m_selectedPathIndex = ui->pathList->currentRow();
    FlightPath *path = getSelectedPath();
    if (!path)
        return;

    const QString jsonPath = canonicalOrAbsolutePath(path->sourceFilePath());
    if (!jsonPath.isEmpty() && QFile::exists(jsonPath)) {
        emit pathJsonLoadRequested(jsonPath);
        return;
    }

    QVector<QVector3D> points;
    for (int i = 0; i < path->waypointCount(); ++i) {
        const Waypoint &wp = path->waypoint(i);
        points.append(QVector3D(wp.x(), wp.y(), wp.z()));
    }
    emit pathLoadRequested(points);
}

void RecordedPathsWidget::onDeletePath()
{
    m_selectedPathIndex = ui->pathList->currentRow();
    FlightPath *path = getSelectedPath();
    if (!path)
        return;

    int ret = QMessageBox::question(this, "Delete Path",
                                   QString("Are you sure you want to delete the path '%1'?\n\nThis will permanently remove the file from disk.").arg(path->name()),
                                   QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        QString pathId = path->id();
        const QString pathsDir = getPathsDirectory();

        if (!deletePathJsonOnDisk(path->sourceFilePath(), pathsDir, path->name())) {
            QMessageBox::warning(this, "Delete Failed",
                                QString("Could not delete the file on disk.\nTried: %1")
                                    .arg(path->sourceFilePath().isEmpty()
                                             ? QDir(pathsDir).filePath(FlightPath::fileBaseFromDisplayName(path->name()) + QStringLiteral(".json"))
                                             : path->sourceFilePath()));
            return;
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
    m_selectedPathIndex = ui->pathList->currentRow();
    FlightPath *path = getSelectedPath();
    if (!path) return;
    
    const QString sanitizedName = FlightPath::fileBaseFromDisplayName(path->name());

    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Path",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + sanitizedName + ".json",
        "JSON Files (*.json)");
    
    if (!fileName.isEmpty()) {
        const QJsonObject sourceRoot = readJsonObject(path->sourceFilePath());
        if (writeJsonPreservingPlannerFields(fileName, *path, sourceRoot)) {
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
        QString destPath;
        if (copyPathIntoCurrentRoom(fileName, &destPath)) {
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
    m_selectedPathIndex = ui->pathList->currentRow();
    FlightPath *path = getSelectedPath();
    if (path) {
        // Save the description if it was modified
        QString newDescription = ui->pathDescriptionEdit->toPlainText();
        if (path->description() != newDescription) {
            path->setDescription(newDescription);
            
            QString filePath = path->sourceFilePath();
            if (filePath.isEmpty() || !QFile::exists(filePath)) {
                const QString pathsDir = getPathsDirectory();
                const QString fileName = FlightPath::fileBaseFromDisplayName(path->name()) + QStringLiteral(".json");
                filePath = QDir(pathsDir).filePath(fileName);
            }

            const QJsonObject sourceRoot = readJsonObject(filePath);
            writeJsonPreservingPlannerFields(filePath, *path, sourceRoot);
        }
        
        const QString jsonPath = canonicalOrAbsolutePath(path->sourceFilePath());
        if (!jsonPath.isEmpty() && QFile::exists(jsonPath)) {
            emit pathJsonLoadRequested(jsonPath);
            return;
        }

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
    m_selectedPathIndex = ui->pathList->currentRow();
    FlightPath *path = getSelectedPath();
    if (!path) return;
    
    FlightPath duplicate = *path;
    duplicate.setSourceFilePath(QString());
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    duplicate.setName(path->name() + QStringLiteral(" (Copy)"));
    duplicate.setCreatedAt(QDateTime::currentDateTime());
    duplicate.setModifiedAt(QDateTime::currentDateTime());

    const QString pathsDir = getPathsDirectory();
    const QString base = FlightPath::fileBaseFromDisplayName(duplicate.name());
    const QString destPath = QDir(pathsDir).filePath(QStringLiteral("%1_%2.json").arg(base, timestamp));
    
    QJsonObject sourceRoot = readJsonObject(path->sourceFilePath());
    if (!sourceRoot.isEmpty()) {
        sourceRoot[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        sourceRoot[QStringLiteral("name")] = duplicate.name();
        sourceRoot[QStringLiteral("created_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    }
    if (writeJsonPreservingPlannerFields(destPath, duplicate, sourceRoot)) {
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

void RecordedPathsWidget::onNewRoom()
{
    if (!m_volumeManager)
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("Create Room"),
                                               QStringLiteral("Room name:"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    const QString id = m_volumeManager->createVolume(name);
    if (id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Create Room"), QStringLiteral("Could not create the room."));
        return;
    }
    emit roomChangeRequested(id);
}

void RecordedPathsWidget::onRenameRoom()
{
    if (!m_volumeManager || !m_volumeManager->hasActiveVolume())
        return;

    const VolumeManager::VolumeInfo room = m_volumeManager->activeVolume();
    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("Rename Room"),
                                               QStringLiteral("Room name:"),
                                               QLineEdit::Normal,
                                               room.name,
                                               &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    if (!m_volumeManager->renameVolume(room.id, name, room.description))
        QMessageBox::warning(this, QStringLiteral("Rename Room"), QStringLiteral("Could not rename the room."));
}

void RecordedPathsWidget::onDeleteRoom()
{
    if (!m_volumeManager || !m_volumeManager->hasActiveVolume())
        return;

    const VolumeManager::VolumeInfo room = m_volumeManager->activeVolume();
    const int pathCount = m_paths.size();
    const QString roomDir = QDir::toNativeSeparators(m_volumeManager->volumeDir(room.id));
    QMessageBox confirmBox(this);
    confirmBox.setIcon(QMessageBox::Warning);
    confirmBox.setWindowTitle(QStringLiteral("Delete Room"));
    confirmBox.setText(QStringLiteral("Delete room \"%1\"?").arg(room.name));
    confirmBox.setInformativeText(
        QStringLiteral("This permanently deletes the room folder, including:\n"
                       "- %1 saved trajector%2\n"
                       "- the saved room map\n"
                       "- any recorded telemetry/trajectory files in the room\n\n"
                       "Folder:\n%3")
            .arg(pathCount)
            .arg(pathCount == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
            .arg(roomDir));
    QPushButton *deleteButton = confirmBox.addButton(QStringLiteral("Delete Room"), QMessageBox::DestructiveRole);
    confirmBox.addButton(QMessageBox::Cancel);
    confirmBox.setDefaultButton(QMessageBox::Cancel);
    confirmBox.exec();

    if (confirmBox.clickedButton() != deleteButton)
        return;

    if (!m_volumeManager->deleteVolume(room.id)) {
        QMessageBox::warning(this,
                             QStringLiteral("Delete Room"),
                             QStringLiteral("Could not delete room \"%1\". Close any files in the room folder and try again.")
                                 .arg(room.name));
        return;
    }

    m_selectedPathIndex = -1;
    m_paths.clear();
    ui->pathList->clear();
    clearPathDetails();
    loadPaths();
}

void RecordedPathsWidget::onImportLegacyPaths()
{
    if (!m_volumeManager || !m_volumeManager->hasActiveVolume()) {
        QMessageBox::information(this, QStringLiteral("Import Old Paths"),
                                 QStringLiteral("Select or create a room before importing old paths."));
        return;
    }

    QStringList candidates;
    for (const QString &dirPath : legacyPathsDirectories()) {
        QDir dir(dirPath);
        const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
        for (const QFileInfo &file : files)
            candidates.append(file.absoluteFilePath());
    }

    if (candidates.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Import Old Paths"),
                                 QStringLiteral("No legacy JSON paths were found outside the selected room."));
        return;
    }

    const int ret = QMessageBox::question(
        this,
        QStringLiteral("Import Old Paths"),
        QStringLiteral("Import %1 legacy path file(s) into room \"%2\"?\n\nOriginal files will be left in place.")
            .arg(candidates.size())
            .arg(m_volumeManager->activeVolume().name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    int copied = 0;
    for (const QString &candidate : candidates) {
        if (copyPathIntoCurrentRoom(candidate))
            ++copied;
    }

    loadPaths();
    QMessageBox::information(this, QStringLiteral("Import Old Paths"),
                             QStringLiteral("Imported %1 of %2 path file(s).").arg(copied).arg(candidates.size()));
}

void RecordedPathsWidget::onCleanupLegacyPaths()
{
    if (!m_volumeManager || !m_volumeManager->hasActiveVolume()) {
        QMessageBox::information(this, QStringLiteral("Clean Old Paths"),
                                 QStringLiteral("Select a room before cleaning old path files."));
        return;
    }

    QStringList deleteCandidates;
    const QDir activeDir(getPathsDirectory());
    for (const QString &dirPath : legacyPathsDirectories()) {
        QDir dir(dirPath);
        const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
        for (const QFileInfo &file : files) {
            if (QFile::exists(activeDir.filePath(file.fileName())))
                deleteCandidates.append(file.absoluteFilePath());
        }
    }

    if (deleteCandidates.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Clean Old Paths"),
                                 QStringLiteral("No duplicate legacy path files were found."));
        return;
    }

    const int ret = QMessageBox::warning(
        this,
        QStringLiteral("Clean Old Paths"),
        QStringLiteral("Delete %1 old duplicate file(s) from the legacy paths folders?\n\nThis cannot be undone.")
            .arg(deleteCandidates.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    int removed = 0;
    for (const QString &path : deleteCandidates) {
        QFile f(path);
        f.setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser);
        if (f.remove())
            ++removed;
    }

    QMessageBox::information(this, QStringLiteral("Clean Old Paths"),
                             QStringLiteral("Deleted %1 of %2 duplicate file(s).").arg(removed).arg(deleteCandidates.size()));
}

void RecordedPathsWidget::updateRoomSummary()
{
    const bool hasRoom = m_volumeManager && m_volumeManager->hasActiveVolume();
    if (m_renameRoomButton)
        m_renameRoomButton->setEnabled(hasRoom);

    if (!m_roomMapLabel)
        return;
    if (!hasRoom) {
        m_roomMapLabel->setText(QStringLiteral("Map: no room selected"));
        return;
    }

    const VolumeManager::MapInfo map = m_volumeManager->activeMapInfo();
    QString status = QStringLiteral("missing");
    if (map.hasBundle())
        status = QStringLiteral("bundle available");
    else if (map.hasMesh())
        status = QStringLiteral("mesh available");
    else if (!map.remotePath.trimmed().isEmpty())
        status = QStringLiteral("remote only");

    m_roomMapLabel->setText(QStringLiteral("Map: %1 (%2)").arg(m_volumeManager->activeVolume().name, status));
}