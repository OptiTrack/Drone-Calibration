#include "mainwindow.h"
#include "widgets/dashboardwidget.h"
#include "widgets/camerafeedwidget.h"
#include "widgets/pathplannerwidget.h"
#include "widgets/recordedpathswidget.h"
#include "widgets/recordedvideoswidget.h"
#include "widgets/dronestatuswidget.h"
#include "controllers/dronecontroller.h"

#include <QApplication>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QIcon>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMetaObject>
#include <QInputDialog>
#include <QLineEdit>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(nullptr)
    , m_centralWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_navigationFrame(nullptr)
    , m_navigationLayout(nullptr)
    , m_navigationList(nullptr)
    , m_drawerToggleButton(nullptr)
    , m_reopenSidebarButton(nullptr)
    , m_connectionStatusDot(nullptr)
    , m_connectionStatusText(nullptr)
    , m_contentStack(nullptr)
    , m_mainSplitter(nullptr)
    , m_dashboardWidget(nullptr)
    , m_cameraFeedWidget(nullptr)
    , m_pathPlannerWidget(nullptr)
    , m_recordedPathsWidget(nullptr)
    , m_recordedVideosWidget(nullptr)
    , m_droneStatusWidget(nullptr)
    , m_droneController(nullptr)
    , m_drawerOpen(true)
    , m_activeView("home")
{
    setWindowTitle("OptiTrack Drone Control - Modal AI Starling 2 Max");
    setMinimumSize(1200, 800);
    resize(1600, 1000);
    
    // Initialize drone controller
    m_droneController = new DroneController(this);
    
    setupUI();
    connectSignals();
    
    // Set initial view
    setActiveView("home");
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    setupNavigationBar();
    setupMainContent();
    setupStatusBar();
    setupPlannerMenus();
    
    // Create main layout
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);
    
    m_mainLayout = new QHBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Create reopen sidebar button (hidden by default)
    // Use hex-encoded UTF-8 for "▶" (Black Right-Pointing Triangle) to avoid encoding issues
    m_reopenSidebarButton = new QPushButton(QString::fromUtf8("\xE2\x96\xB6"));
    m_reopenSidebarButton->setFixedWidth(32);
    m_reopenSidebarButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_reopenSidebarButton->setToolTip("Open Sidebar");
    m_reopenSidebarButton->setCursor(Qt::PointingHandCursor);
    m_reopenSidebarButton->hide(); // Initially hidden as sidebar is open
    m_reopenSidebarButton->setStyleSheet(
        "QPushButton { "
        "   background-color: #2d2d2d; "
        "   color: #ffffff; "
        "   border: none; "
        "   border-right: 1px solid #555555; "
        "   font-weight: bold; "
        "   font-size: 14px; "
        "} "
        "QPushButton:hover { "
        "   background-color: #404040; "
        "   color: #0099ff; "
        "}"
    );
    m_mainLayout->addWidget(m_reopenSidebarButton);
    
    // Create main splitter
    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainLayout->addWidget(m_mainSplitter);
    
    // Add navigation and content to splitter
    m_mainSplitter->addWidget(m_navigationFrame);
    m_mainSplitter->addWidget(m_contentStack);
    
    // Set splitter proportions
    m_mainSplitter->setSizes({250, 1350});
    m_mainSplitter->setCollapsible(0, true);
    m_mainSplitter->setCollapsible(1, false);
}

void MainWindow::setupPlannerMenus()
{
    QMenu *connectionMenu = menuBar()->addMenu("Connection");
    QAction *connectAction = connectionMenu->addAction("Connect to Starling 2 Max");
    QAction *disconnectAction = connectionMenu->addAction("Disconnect");

    connect(connectAction, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString host = QInputDialog::getText(this,
                                                   "Connect to Starling 2 Max",
                                                   "VOXL IP address:",
                                                   QLineEdit::Normal,
                                                   "192.168.8.1",
                                                   &ok);
        if (!ok || host.trimmed().isEmpty()) {
            return;
        }

        const int port = QInputDialog::getInt(this,
                                              "Connect to Starling 2 Max",
                                              "MAVLink UDP port:",
                                              14550,
                                              1,
                                              65535,
                                              1,
                                              &ok);
        if (!ok) {
            return;
        }

        m_droneController->connectToDrone(host.trimmed(), port);
    });

    connect(disconnectAction, &QAction::triggered,
            m_droneController, &DroneController::disconnectFromDrone);

    QMenu *missionMenu = menuBar()->addMenu("Mission");
    QAction *uploadAction = missionMenu->addAction("Upload Mission");
    QAction *runAction = missionMenu->addAction("Run Mission");

    auto invokePlannerSlot = [this](const char *slotName) {
        if (!m_pathPlannerWidget)
            return;
        QMetaObject::invokeMethod(m_pathPlannerWidget, slotName, Qt::QueuedConnection);
    };

    connect(uploadAction, &QAction::triggered, this, [invokePlannerSlot]() { invokePlannerSlot("onUploadMission"); });
    connect(runAction, &QAction::triggered, this, [invokePlannerSlot]() { invokePlannerSlot("onRunMission"); });
}

void MainWindow::setupNavigationBar()
{
    // Create navigation frame with Motive-inspired styling
    m_navigationFrame = new QFrame;
    m_navigationFrame->setFrameShape(QFrame::NoFrame);
    m_navigationFrame->setMinimumWidth(220);
    m_navigationFrame->setMaximumWidth(320);
    m_navigationFrame->setStyleSheet(
        "QFrame { "
        "   background-color: #323232; "
        "   border: none; "
        "   border-right: 2px solid #555555; "
        "}"
    );
    
    m_navigationLayout = new QVBoxLayout(m_navigationFrame);
    m_navigationLayout->setContentsMargins(0, 0, 0, 0);
    m_navigationLayout->setSpacing(0);
    
    // Create OptiTrack-style header with logo area
    QFrame *headerFrame = new QFrame;
    headerFrame->setFrameShape(QFrame::NoFrame);
    headerFrame->setFixedHeight(60);
    headerFrame->setStyleSheet(
        "QFrame { "
        "   background-color: #2d2d2d; "
        "   border: none; "
        "   border-bottom: 2px solid #007acc; "
        "}"
    );
    
    QHBoxLayout *headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    
    // OptiTrack branding
    QLabel *brandLabel = new QLabel("OptiTrack Drone");
    brandLabel->setStyleSheet(
        "QLabel { "
        "   color: #007acc; "
        "   font-size: 16px; "
        "   font-weight: bold; "
        "}"
    );
    headerLayout->addWidget(brandLabel);
    
    // Drawer toggle button
    // Use hex-encoded UTF-8 for "◀" (Black Left-Pointing Triangle)
    m_drawerToggleButton = new QPushButton(QString::fromUtf8("\xE2\x97\x80"));
    m_drawerToggleButton->setFixedSize(32, 32);
    m_drawerToggleButton->setStyleSheet(
        "QPushButton { "
        "   background-color: #3c3c3c; "
        "   color: #dcdcdc; "
        "   border: 1px solid #555555; "
        "   border-radius: 4px; "
        "   font-size: 18px; "
        "   font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "   background-color: #4a4a4a; "
        "   color: #007acc; "
        "   border-color: #007acc; "
        "}"
    );
    headerLayout->addWidget(m_drawerToggleButton);
    
    m_navigationLayout->addWidget(headerFrame);
    
    // Create navigation list with Motive styling
    m_navigationList = new QListWidget;
    m_navigationList->setFocusPolicy(Qt::NoFocus);  // Disable focus rectangle
    m_navigationList->setStyleSheet(
        "QListWidget { "
        "   background-color: #323232; "
        "   border: none; "
        "   color: #dcdcdc; "
        "   outline: none; "
        "} "
        "QListWidget:focus { "
        "   outline: none; "
        "   border: none; "
        "} "
        "QListWidget::item { "
        "   padding: 12px 16px; "
        "   border-bottom: 1px solid #555555; "
        "   font-size: 14px; "
        "   outline: none; "
        "} "
        "QListWidget::item:focus { "
        "   outline: none; "
        "   border: none; "
        "} "
        "QListWidget::item:hover { "
        "   background-color: #404040; "
        "   color: white; "
        "} "
        "QListWidget::item:selected { "
        "   background-color: #4a4a4a; "
        "   color: white; "
        "   font-weight: bold; "
        "   outline: none; "
        "}"
    );
    
    // Add navigation items with professional styling
    struct NavItem {
        QString text;
        QString icon;
        QString description;
    };
    
    QList<NavItem> navItems = {
        {"Home", "●", "Dashboard overview"},
        {"Live Camera", "◐", "Real-time camera feed"}, 
        {"Mission", "◢", "Plan and execute missions"},
        {"Saved Paths", "◫", "View saved flight paths"},
        {"Media Library", "◨", "Recorded videos"},
        {"System Status", "◉", "Drone telemetry"}
    };
    
    for (const NavItem &item : navItems) {
        QListWidgetItem *listItem = new QListWidgetItem;
        
        // Create custom widget for navigation item
        QWidget *itemWidget = new QWidget;
        itemWidget->setStyleSheet(
            "QWidget { "
            "   background: transparent; "
            "   border: none; "
            "}"
        );
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(8, 4, 8, 4);
        itemLayout->setSpacing(12);
        
        // Icon label
        QLabel *iconLabel = new QLabel(item.icon);
        iconLabel->setFixedSize(20, 20);
        iconLabel->setStyleSheet(
            "QLabel { "
            "   color: #007acc; "
            "   font-size: 16px; "
            "   font-weight: bold; "
            "   background: transparent; "
            "   border: none; "
            "}"
        );
        iconLabel->setAlignment(Qt::AlignCenter);
        
        // Text layout
        QVBoxLayout *textLayout = new QVBoxLayout;
        textLayout->setSpacing(2);
        textLayout->setContentsMargins(0, 0, 0, 0);
        
        QLabel *titleLabel = new QLabel(item.text);
        titleLabel->setStyleSheet(
            "QLabel { "
            "   color: #dcdcdc; "
            "   font-size: 14px; "
            "   font-weight: bold; "
            "   background: transparent; "
            "   border: none; "
            "}"
        );
        
        QLabel *descLabel = new QLabel(item.description);
        descLabel->setStyleSheet(
            "QLabel { "
            "   color: #999999; "
            "   font-size: 11px; "
            "   background: transparent; "
            "   border: none; "
            "}"
        );
        
        textLayout->addWidget(titleLabel);
        textLayout->addWidget(descLabel);
        
        itemLayout->addWidget(iconLabel);
        itemLayout->addLayout(textLayout);
        itemLayout->addStretch();
        
        listItem->setSizeHint(QSize(0, 60));
        m_navigationList->addItem(listItem);
        m_navigationList->setItemWidget(listItem, itemWidget);
    }
    
    m_navigationLayout->addWidget(m_navigationList);
    
    // Add connection status footer
    QFrame *statusFooter = new QFrame;
    statusFooter->setFrameShape(QFrame::NoFrame);
    statusFooter->setFixedHeight(80);
    statusFooter->setStyleSheet(
        "QFrame { "
        "   background-color: #2d2d2d; "
        "   border: none; "
        "   border-top: 1px solid #555555; "
        "}"
    );
    
    QVBoxLayout *statusLayout = new QVBoxLayout(statusFooter);
    statusLayout->setContentsMargins(12, 8, 12, 8);
    statusLayout->setSpacing(4);
    
    // Connection status
    QHBoxLayout *connectionLayout = new QHBoxLayout;
    m_connectionStatusDot = new QLabel(QString::fromUtf8("\xE2\x97\x8F"));
    m_connectionStatusDot->setStyleSheet("color: #ef4444; font-size: 12px;");
    
    m_connectionStatusText = new QLabel("Drone Disconnected");
    m_connectionStatusText->setStyleSheet(
        "QLabel { color: #dcdcdc; font-size: 12px; }"
    );
    
    connectionLayout->addWidget(m_connectionStatusDot);
    connectionLayout->addWidget(m_connectionStatusText);
    connectionLayout->addStretch();
    
    // Version info
    QLabel *versionLabel = new QLabel("v1.0 - OptiTrack");
    versionLabel->setStyleSheet(
        "QLabel { color: #999999; font-size: 10px; }"
    );
    
    statusLayout->addLayout(connectionLayout);
    statusLayout->addWidget(versionLabel);
    statusLayout->addStretch();
    
    m_navigationLayout->addWidget(statusFooter);
    
    // Select first item
    m_navigationList->setCurrentRow(0);
}

void MainWindow::setupMainContent()
{
    // Create content stack
    m_contentStack = new QStackedWidget;
    
    // Index 0: Home - Dashboard
    m_dashboardWidget = new DashboardWidget;
    m_contentStack->addWidget(m_dashboardWidget);
    
    // Index 1: Live Camera
    m_cameraFeedWidget = new CameraFeedWidget;
    m_contentStack->addWidget(m_cameraFeedWidget);
    
    // Index 2: Mission
    m_pathPlannerWidget = new PathPlannerWidget;
    m_pathPlannerWidget->setDroneController(m_droneController);  // Connect drone controller
    m_contentStack->addWidget(m_pathPlannerWidget);
    
    // Index 3: Flight History (Recorded Paths)
    m_recordedPathsWidget = new RecordedPathsWidget;
    m_contentStack->addWidget(m_recordedPathsWidget);
    
    // Index 4: Media Library (Recorded Videos)
    m_recordedVideosWidget = new RecordedVideosWidget;
    m_contentStack->addWidget(m_recordedVideosWidget);
    
    // Index 5: System Status (Drone Status)
    m_droneStatusWidget = new DroneStatusWidget;
    m_contentStack->addWidget(m_droneStatusWidget);
}

void MainWindow::setupStatusBar()
{
    statusBar()->showMessage("Ready - Disconnected from drone");
    statusBar()->setStyleSheet(
        "QStatusBar { "
        "   background-color: #374151; "
        "   color: white; "
        "   border-top: 1px solid #4b5563; "
        "}"
    );
}

void MainWindow::connectSignals()
{
    // Navigation
    connect(m_navigationList, &QListWidget::currentRowChanged,
            this, &MainWindow::onNavigationItemClicked);
    connect(m_drawerToggleButton, &QPushButton::clicked,
            this, &MainWindow::onDrawerToggled);
    connect(m_reopenSidebarButton, &QPushButton::clicked,
            this, &MainWindow::onDrawerToggled);
    
    // Dashboard navigation signals
    connect(m_dashboardWidget, &DashboardWidget::navigateToCamera,
            this, [this]() { setActiveView("camera"); });
    connect(m_dashboardWidget, &DashboardWidget::navigateToPlanner,
            this, [this]() { setActiveView("planner"); });
    connect(m_dashboardWidget, &DashboardWidget::navigateToHistory,
            this, [this]() { setActiveView("paths"); });
    connect(m_dashboardWidget, &DashboardWidget::navigateToMedia,
            this, [this]() { setActiveView("videos"); });
    connect(m_dashboardWidget, &DashboardWidget::navigateToStatus,
            this, [this]() { setActiveView("status"); });
    
    // Path planner signals
    connect(m_pathPlannerWidget, &PathPlannerWidget::pathSaved,
            this, &MainWindow::onPathSaved);
    connect(m_pathPlannerWidget, &PathPlannerWidget::pathSaved,
            m_recordedPathsWidget, &RecordedPathsWidget::addPath);
    
    // Recorded paths signals
    connect(m_recordedPathsWidget, &RecordedPathsWidget::pathDeleted,
            this, &MainWindow::onPathDeleted);
    connect(m_recordedPathsWidget, &RecordedPathsWidget::pathLoadRequested,
            this, &MainWindow::onPathLoadRequested);
    connect(m_recordedPathsWidget, &RecordedPathsWidget::pathJsonLoadRequested,
            this, &MainWindow::onPathJsonLoadRequested);
    
    // Camera feed signals
    connect(m_cameraFeedWidget, &CameraFeedWidget::recordingSaved,
            this, &MainWindow::onRecordingSaved);
    connect(m_cameraFeedWidget, &CameraFeedWidget::recordingSaved,
            m_recordedVideosWidget, &RecordedVideosWidget::addRecording);
    
    // Recorded videos signals
    connect(m_recordedVideosWidget, &RecordedVideosWidget::recordingDeleted,
            this, &MainWindow::onRecordingDeleted);
    connect(m_recordedVideosWidget, &RecordedVideosWidget::recordingPlayRequested,
            this, &MainWindow::onRecordingPlayRequested);
    
    // Drone controller signals
    connect(m_droneController, &DroneController::connectionStatusChanged,
            this, [this](bool connected) {
                QString status = connected ? "Connected to VOXL 2" : "Disconnected from drone";
                statusBar()->showMessage(status);
                m_connectionStatusDot->setStyleSheet(
                    connected ? "color: #10b981; font-size: 12px;" : "color: #ef4444; font-size: 12px;");
                m_connectionStatusText->setText(connected ? "Drone Connected" : "Drone Disconnected");
                m_droneStatusWidget->setConnectionStatus(connected);
            });
    connect(m_droneController, &DroneController::statusUpdated,
            m_droneStatusWidget, &DroneStatusWidget::updateDroneStatus);
    connect(m_droneController, &DroneController::messageReceived,
            this, [this](const QString &message) { statusBar()->showMessage(message, 5000); });
    connect(m_droneController, &DroneController::errorOccurred,
            this, [this](const QString &error) { statusBar()->showMessage(error, 7000); });

    connect(m_droneStatusWidget, &DroneStatusWidget::armDisarmRequested,
            m_droneController, &DroneController::armDrone);
    connect(m_droneStatusWidget, &DroneStatusWidget::takeoffRequested,
            m_droneController, [this]() { m_droneController->takeoff(10.0f); });
    connect(m_droneStatusWidget, &DroneStatusWidget::landRequested,
            m_droneController, &DroneController::land);
    connect(m_droneStatusWidget, &DroneStatusWidget::returnToLaunchRequested,
            m_droneController, &DroneController::returnToLaunch);
    connect(m_droneStatusWidget, &DroneStatusWidget::forceDisarmRequested,
            m_droneController, &DroneController::forceDisarm);
    connect(m_droneStatusWidget, &DroneStatusWidget::flightTerminationRequested,
            m_droneController, &DroneController::flightTermination);
}

void MainWindow::onNavigationItemClicked(int index)
{
    m_contentStack->setCurrentIndex(index);
    
    QStringList viewNames = {"home", "camera", "planner", "paths", "videos", "status"};
    if (index >= 0 && index < viewNames.size()) {
        m_activeView = viewNames[index];
    }
}

void MainWindow::onDrawerToggled()
{
    m_drawerOpen = !m_drawerOpen;
    m_navigationFrame->setVisible(m_drawerOpen);
    m_reopenSidebarButton->setVisible(!m_drawerOpen);
}

void MainWindow::onPathSaved(const QString &name, const QVector<QVector3D> &points)
{
    // Path is automatically added to recorded paths via signal connection
    statusBar()->showMessage(QString("Path '%1' saved successfully").arg(name), 3000);
}

void MainWindow::onPathDeleted(const QString &pathId)
{
    statusBar()->showMessage("Path deleted successfully", 3000);
}

void MainWindow::onPathLoadRequested(const QVector<QVector3D> &points)
{
    m_draftPoints = points;
    m_pathPlannerWidget->loadPoints(points);
    m_contentStack->setCurrentIndex(2); // Switch to path planner
    m_navigationList->setCurrentRow(2);
    m_activeView = "planner";
}

void MainWindow::onPathJsonLoadRequested(const QString &absoluteJsonPath)
{
    if (m_pathPlannerWidget->loadPathFromFile(absoluteJsonPath, false))
        statusBar()->showMessage(QStringLiteral("Path loaded from %1").arg(absoluteJsonPath), 4000);
    else
        statusBar()->showMessage(QStringLiteral("Failed to load %1").arg(absoluteJsonPath), 5000);

    m_contentStack->setCurrentIndex(2);
    m_navigationList->setCurrentRow(2);
    m_activeView = "planner";
}

void MainWindow::onRecordingSaved(const QString &filePath, const QByteArray &data)
{
    // Recording is automatically added to recorded videos via signal connection
    statusBar()->showMessage("Recording saved successfully", 3000);
}

void MainWindow::onRecordingDeleted(const QString &recordingId)
{
    statusBar()->showMessage("Recording deleted successfully", 3000);
}

void MainWindow::onRecordingPlayRequested(const QString &filePath)
{
    // For now, just show a message. In a full implementation, 
    // this would open the recording in a media player
    QMessageBox::information(this, "Play Recording", 
                           QString("Playing recording: %1").arg(filePath));
}

void MainWindow::setActiveView(const QString &viewName)
{
    m_activeView = viewName;
    
    QStringList viewNames = {"home", "camera", "planner", "paths", "videos", "status"};
    int index = viewNames.indexOf(viewName);
    if (index >= 0) {
        m_contentStack->setCurrentIndex(index);
        m_navigationList->setCurrentRow(index);
    }
}
