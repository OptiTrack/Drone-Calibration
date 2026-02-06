#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include <QWidget>
#include <QFrame>
#include <QLabel>

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
    void setupStatusCards();
    void setupConnections();
    QFrame* createStatusCard(const QString &title, const QString &value, 
                             const QString &subtitle, const QString &color);
    
    Ui::DashboardWidget *ui;
};

#endif // DASHBOARDWIDGET_H

