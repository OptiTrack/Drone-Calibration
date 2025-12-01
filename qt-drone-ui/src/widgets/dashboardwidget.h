#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QGroupBox>

QT_BEGIN_NAMESPACE
namespace Ui { class DashboardWidget; }
QT_END_NAMESPACE

class DashboardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    ~DashboardWidget();

signals:
    void navigateToCamera();
    void navigateToPlanner();
    void navigateToHistory();
    void navigateToMedia();
    void navigateToStatus();

private:
    void setupUI();
    QFrame* createQuickActionCard(const QString &title, const QString &description, 
                                   const QString &icon, const QString &buttonText);
    QFrame* createStatusCard(const QString &title, const QString &value, 
                             const QString &subtitle, const QString &color);
    
    Ui::DashboardWidget *ui;
    
    // UI Components
    QVBoxLayout *m_mainLayout;
    QLabel *m_welcomeLabel;
    QLabel *m_subtitleLabel;
    QGridLayout *m_cardsLayout;
    QHBoxLayout *m_statusLayout;
};

#endif // DASHBOARDWIDGET_H

