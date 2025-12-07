#include "dashboardwidget.h"

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent)
    , ui(nullptr)
    , m_mainLayout(nullptr)
    , m_welcomeLabel(nullptr)
    , m_subtitleLabel(nullptr)
    , m_cardsLayout(nullptr)
    , m_statusLayout(nullptr)
{
    setupUI();
}

DashboardWidget::~DashboardWidget()
{
}

void DashboardWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(30, 30, 30, 30);
    m_mainLayout->setSpacing(20);
    
    // Welcome header
    QWidget *headerWidget = new QWidget;
    QVBoxLayout *headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    
    m_welcomeLabel = new QLabel("OptiTrack Drone Control");
    m_welcomeLabel->setStyleSheet(
        "QLabel { "
        "   color: #ffffff; "
        "   font-size: 28px; "
        "   font-weight: bold; "
        "   background: transparent; "
        "}"
    );
    
    m_subtitleLabel = new QLabel("Modal AI Starling 2 Max • Dashboard Overview");
    m_subtitleLabel->setStyleSheet(
        "QLabel { "
        "   color: #9ca3af; "
        "   font-size: 14px; "
        "   background: transparent; "
        "}"
    );
    
    headerLayout->addWidget(m_welcomeLabel);
    headerLayout->addWidget(m_subtitleLabel);
    m_mainLayout->addWidget(headerWidget);
    
    // Status cards row
    m_statusLayout = new QHBoxLayout;
    m_statusLayout->setSpacing(15);
    
    m_statusLayout->addWidget(createStatusCard("Connection", "Online", "Drone connected", "#22c55e"));
    m_statusLayout->addWidget(createStatusCard("Battery", "87%", "Estimated 24 min", "#3b82f6"));
    m_statusLayout->addWidget(createStatusCard("GPS", "Fixed", "12 satellites", "#22c55e"));
    m_statusLayout->addWidget(createStatusCard("Signal", "Strong", "-45 dBm", "#22c55e"));
    
    m_mainLayout->addLayout(m_statusLayout);
    
    // Quick actions section
    QLabel *actionsLabel = new QLabel("Quick Actions");
    actionsLabel->setStyleSheet(
        "QLabel { "
        "   color: #ffffff; "
        "   font-size: 18px; "
        "   font-weight: bold; "
        "   background: transparent; "
        "   margin-top: 10px; "
        "}"
    );
    m_mainLayout->addWidget(actionsLabel);
    
    // Action cards grid
    m_cardsLayout = new QGridLayout;
    m_cardsLayout->setSpacing(15);
    
    // Create action cards
    QFrame *cameraCard = createQuickActionCard(
        "Live Camera", 
        "View real-time camera feed from the drone",
        "◐", 
        "Open Camera"
    );
    connect(cameraCard->findChild<QPushButton*>(), &QPushButton::clicked, 
            this, &DashboardWidget::navigateToCamera);
    
    QFrame *plannerCard = createQuickActionCard(
        "Flight Planner", 
        "Create and edit waypoint flight paths",
        "◢", 
        "Open Planner"
    );
    connect(plannerCard->findChild<QPushButton*>(), &QPushButton::clicked, 
            this, &DashboardWidget::navigateToPlanner);
    
    QFrame *historyCard = createQuickActionCard(
        "Flight History", 
        "View and manage recorded flight paths",
        "◫", 
        "View History"
    );
    connect(historyCard->findChild<QPushButton*>(), &QPushButton::clicked, 
            this, &DashboardWidget::navigateToHistory);
    
    QFrame *mediaCard = createQuickActionCard(
        "Media Library", 
        "Browse recorded videos and images",
        "◨", 
        "Open Library"
    );
    connect(mediaCard->findChild<QPushButton*>(), &QPushButton::clicked, 
            this, &DashboardWidget::navigateToMedia);
    
    m_cardsLayout->addWidget(cameraCard, 0, 0);
    m_cardsLayout->addWidget(plannerCard, 0, 1);
    m_cardsLayout->addWidget(historyCard, 1, 0);
    m_cardsLayout->addWidget(mediaCard, 1, 1);
    
    m_mainLayout->addLayout(m_cardsLayout);
    
    // Recent activity section
    QLabel *recentLabel = new QLabel("Recent Activity");
    recentLabel->setStyleSheet(
        "QLabel { "
        "   color: #ffffff; "
        "   font-size: 18px; "
        "   font-weight: bold; "
        "   background: transparent; "
        "   margin-top: 10px; "
        "}"
    );
    m_mainLayout->addWidget(recentLabel);
    
    // Recent activity placeholder
    QFrame *recentFrame = new QFrame;
    recentFrame->setFrameShape(QFrame::NoFrame);
    recentFrame->setStyleSheet(
        "QFrame { "
        "   background-color: #1f2937; "
        "   border: none; "
        "   border-radius: 8px; "
        "}"
    );
    recentFrame->setMinimumHeight(120);
    
    QVBoxLayout *recentLayout = new QVBoxLayout(recentFrame);
    recentLayout->setContentsMargins(20, 20, 20, 20);
    
    QLabel *noActivityLabel = new QLabel("No recent activity");
    noActivityLabel->setStyleSheet(
        "QLabel { "
        "   color: #6b7280; "
        "   font-size: 14px; "
        "   background: transparent; "
        "}"
    );
    noActivityLabel->setAlignment(Qt::AlignCenter);
    
    QLabel *hintLabel = new QLabel("Start by creating a flight path or viewing the live camera feed");
    hintLabel->setStyleSheet(
        "QLabel { "
        "   color: #4b5563; "
        "   font-size: 12px; "
        "   background: transparent; "
        "}"
    );
    hintLabel->setAlignment(Qt::AlignCenter);
    
    recentLayout->addWidget(noActivityLabel);
    recentLayout->addWidget(hintLabel);
    
    m_mainLayout->addWidget(recentFrame);
    
    // Add stretch to push everything up
    m_mainLayout->addStretch();
}

QFrame* DashboardWidget::createQuickActionCard(const QString &title, const QString &description, 
                                                const QString &icon, const QString &buttonText)
{
    QFrame *card = new QFrame;
    card->setFrameShape(QFrame::NoFrame);
    card->setStyleSheet(
        "QFrame { "
        "   background-color: #1f2937; "
        "   border: none; "
        "   border-radius: 8px; "
        "}"
    );
    card->setMinimumHeight(140);
    
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);
    
    // Icon
    QLabel *iconLabel = new QLabel(icon);
    iconLabel->setStyleSheet(
        "QLabel { "
        "   color: #3b82f6; "
        "   font-size: 24px; "
        "   background: transparent; "
        "}"
    );
    
    // Title
    QLabel *titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(
        "QLabel { "
        "   color: #ffffff; "
        "   font-size: 16px; "
        "   font-weight: bold; "
        "   background: transparent; "
        "}"
    );
    
    // Description
    QLabel *descLabel = new QLabel(description);
    descLabel->setStyleSheet(
        "QLabel { "
        "   color: #9ca3af; "
        "   font-size: 12px; "
        "   background: transparent; "
        "}"
    );
    descLabel->setWordWrap(true);
    
    // Button
    QPushButton *button = new QPushButton(buttonText);
    button->setStyleSheet(
        "QPushButton { "
        "   background-color: #374151; "
        "   color: #ffffff; "
        "   border: none; "
        "   border-radius: 4px; "
        "   padding: 8px 16px; "
        "   font-size: 12px; "
        "} "
        "QPushButton:hover { "
        "   background-color: #4b5563; "
        "}"
    );
    
    layout->addWidget(iconLabel);
    layout->addWidget(titleLabel);
    layout->addWidget(descLabel);
    layout->addStretch();
    layout->addWidget(button);
    
    return card;
}

QFrame* DashboardWidget::createStatusCard(const QString &title, const QString &value, 
                                          const QString &subtitle, const QString &color)
{
    QFrame *card = new QFrame;
    card->setFrameShape(QFrame::NoFrame);
    card->setStyleSheet(
        "QFrame { "
        "   background-color: #1f2937; "
        "   border: none; "
        "   border-radius: 8px; "
        "}"
    );
    card->setMinimumWidth(150);
    card->setMinimumHeight(90);
    
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(4);
    
    // Title
    QLabel *titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(
        "QLabel { "
        "   color: #9ca3af; "
        "   font-size: 12px; "
        "   background: transparent; "
        "}"
    );
    
    // Value with color
    QLabel *valueLabel = new QLabel(value);
    valueLabel->setStyleSheet(
        QString("QLabel { "
        "   color: %1; "
        "   font-size: 20px; "
        "   font-weight: bold; "
        "   background: transparent; "
        "}").arg(color)
    );
    
    // Subtitle
    QLabel *subtitleLabel = new QLabel(subtitle);
    subtitleLabel->setStyleSheet(
        "QLabel { "
        "   color: #6b7280; "
        "   font-size: 11px; "
        "   background: transparent; "
        "}"
    );
    
    layout->addWidget(titleLabel);
    layout->addWidget(valueLabel);
    layout->addWidget(subtitleLabel);
    
    return card;
}

