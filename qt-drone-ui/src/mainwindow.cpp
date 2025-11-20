#include "mainwindow.h"
#include "widgets/camerafeedwidget.h"
#include "widgets/pathplannerwidget.h"
#include "widgets/recordedpathswidget.h"
#include "widgets/recordedvideoswidget.h"
#include "widgets/dronestatuswidget.h"
#include "controllers/dronecontroller.h"

#include <QApplication>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolBar>
#include <QIcon>
#include <QListWidgetItem>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(nullptr)
    , m_centralWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_navigationFrame(nullptr)
    , m_navigationLayout(nullptr)
    , m_navigationList(nullptr)
    , m_drawerToggleButton(nullptr)
    , m_contentStack(nullptr)
    , m_mainSplitter(nullptr)
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
    
    // Create main layout
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);
    
    m_mainLayout = new QHBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
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

void MainWindow::setupNavigationBar()
{
    // Create navigation frame with Motive-inspired styling
    m_navigationFrame = new QFrame;
    m_navigationFrame->setFrameStyle(QFrame::StyledPanel);
    m_navigationFrame->setMinimumWidth(220);
    m_navigationFrame->setMaximumWidth(320);
    m_navigationFrame->setStyleSheet(
        "QFrame { "
        "   background-color: #323232; "
        "   border-right: 2px solid #555555; "
        "}"
    );
    
    m_navigationLayout = new QVBoxLayout(m_navigationFrame);
    m_navigationLayout->setContentsMargins(0, 0, 0, 0);
    m_navigationLayout->setSpacing(0);
    
    // Create OptiTrack-style header with logo area
    QFrame *headerFrame = new QFrame;
    headerFrame->setFixedHeight(60);
    headerFrame->setStyleSheet(
        "QFrame { "
        "   background-color: #2d2d2d; "
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
    m_drawerToggleButton = new QPushButton("Menu");
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
        "   background-color: #007acc; "
        "   border-color: #0099ff; "
        "}"
    );
    headerLayout->addWidget(m_drawerToggleButton);
    
    m_navigationLayout->addWidget(headerFrame);
    
    // Create navigation list with Motive styling
    m_navigationList = new QListWidget;
    m_navigationList->setStyleSheet(
        "QListWidget { "
        "   background-color: #323232; "
        "   border: none; "
        "   color: #dcdcdc; "
        "   outline: 0; "
        "} "
        "QListWidget::item { "
        "   padding: 12px 16px; "
        "   border-bottom: 1px solid #555555; "
        "   font-size: 14px; "
        "} "
        "QListWidget::item:hover { "
        "   background-color: #404040; "
        "   color: white; "
        "} "
        "QListWidget::item:selected { "
        "   background-color: #007acc; "
        "   color: white; "
        "   font-weight: bold; "
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
        {"Flight Planner", "◢", "Plan drone waypoints"},
        {"Flight History", "◫", "View recorded paths"},
        {"Media Library", "◨", "Recorded videos"},
        {"System Status", "◉", "Drone telemetry"}
    };
    
    for (const NavItem &item : navItems) {
        QListWidgetItem *listItem = new QListWidgetItem;
        
        // Create custom widget for navigation item
        QWidget *itemWidget = new QWidget;
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
            "}"
        );
        
        QLabel *descLabel = new QLabel(item.description);
        descLabel->setStyleSheet(
            "QLabel { "
            "   color: #999999; "
            "   font-size: 11px; "
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
    statusFooter->setFixedHeight(80);
    statusFooter->setStyleSheet(
        "QFrame { "
        "   background-color: #2d2d2d; "
        "   border-top: 1px solid #555555; "
        "}"
    );
    
    QVBoxLayout *statusLayout = new QVBoxLayout(statusFooter);
    statusLayout->setContentsMargins(12, 8, 12, 8);
    statusLayout->setSpacing(4);
    
    // Connection status
    QHBoxLayout *connectionLayout = new QHBoxLayout;
    QLabel *statusDot = new QLabel("●");
    statusDot->setStyleSheet("color: #28a745; font-size: 12px;"); // Green for connected
    
    QLabel *statusText = new QLabel("Drone Connected");
    statusText->setStyleSheet(
        "QLabel { color: #dcdcdc; font-size: 12px; }"
    );
    
    connectionLayout->addWidget(statusDot);
    connectionLayout->addWidget(statusText);
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
    
    // Create home page (camera feed)
    m_cameraFeedWidget = new CameraFeedWidget;
    m_contentStack->addWidget(m_cameraFeedWidget);
    
    // Create camera feed page (duplicate for navigation consistency)
    m_contentStack->addWidget(m_cameraFeedWidget);
    
    // Create path planner page
    m_pathPlannerWidget = new PathPlannerWidget;
    m_contentStack->addWidget(m_pathPlannerWidget);
    
    // Create recorded paths page
    m_recordedPathsWidget = new RecordedPathsWidget;
    m_contentStack->addWidget(m_recordedPathsWidget);
    
    // Create recorded videos page
    m_recordedVideosWidget = new RecordedVideosWidget;
    m_contentStack->addWidget(m_recordedVideosWidget);
    
    // Create drone status page
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
            });
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