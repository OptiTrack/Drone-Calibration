#include "dashboardwidget.h"
#include "ui_dashboardwidget.h"

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DashboardWidget)
{
    ui->setupUi(this);
    setupStatusCards();
    setupConnections();
}

DashboardWidget::~DashboardWidget()
{
    delete ui;
}

void DashboardWidget::setupStatusCards()
{
    // Status cards are now fully defined in the .ui file
    // No programmatic setup needed - all labels are created in Qt Designer
}

void DashboardWidget::setupConnections()
{
    // Connect action card buttons to navigation signals
    connect(ui->cameraButton, &QPushButton::clicked, this, &DashboardWidget::navigateToCamera);
    connect(ui->plannerButton, &QPushButton::clicked, this, &DashboardWidget::navigateToPlanner);
    connect(ui->historyButton, &QPushButton::clicked, this, &DashboardWidget::navigateToHistory);
    connect(ui->mediaButton, &QPushButton::clicked, this, &DashboardWidget::navigateToMedia);
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
