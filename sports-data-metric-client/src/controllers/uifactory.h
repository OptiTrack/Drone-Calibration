#ifndef UIFACTORY_H
#define UIFACTORY_H

#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QList>
#include <QString>
#include <QJsonArray>
#include <QTableWidget>
#include <QComboBox>
#include <QHeaderView>
#include <QPushButton>
#include <QListWidget>

#include "settings.h"
#include "./src/widgets/graphwidget.h"

struct ConnectionWidgets {
    QString name = QString();
    QGroupBox *groupBox = new QGroupBox();
    QTableWidget *tableWidget = new QTableWidget();
    QTableWidgetItem *serverIP = new QTableWidgetItem();
    QTableWidgetItem *clientIP = new QTableWidgetItem();
    QComboBox *connectionTypes = new QComboBox();
    QComboBox *namingConventions = new QComboBox();
    QTableWidgetItem *connectedStatus = new QTableWidgetItem();
    QPushButton *connectButton = new QPushButton();
};

struct TakeWidgets {
    QString name = QString();
    QGroupBox *groupBox = new QGroupBox();
    QListWidget *listWidget = new QListWidget();
    QComboBox *playSpeed = new QComboBox();
    QPushButton *loadButton = new QPushButton();
    QPushButton *runButton = new QPushButton();
};

struct SportsWidgets {
    QString name = QString();
    QGroupBox *groupBox = new QGroupBox();
    QComboBox *sportTypes = new QComboBox();
};

struct AssetWidgets {
    QString name = QString();
    QGroupBox *groupBox = new QGroupBox();
    QTableWidget *tableWidget = new QTableWidget();
    QComboBox *skeletonTypes = new QComboBox();
    QComboBox *rigidBodyTypes = new QComboBox();
};

struct MetricWidgets {
    QString name = QString();
    QString units = QString();
    QGroupBox *groupBox = new QGroupBox();
    QVector<QLabel*> *dataLabels = new QVector<QLabel*>();
    QVector<QLabel*> *descriptionLabels = new QVector<QLabel*>();
    QVector<GraphWidget*> *metricGraphs = new QVector<GraphWidget*>();
};

class UiFactory : public QObject {
    Q_OBJECT

public:
    UiFactory();
    ConnectionWidgets *createConnectionWidgets(const QString name, ConnectionSettings connectionSettings);
    TakeWidgets *createTakeWidgets(const QString name);
    SportsWidgets *createSportsWidgets(const QString name);
    AssetWidgets *createAssetWidgets(const QString name);
    MetricWidgets *createMetricWidgets(const QString name, const QString units, QVector<QString> labels, QVector<QString> descriptions, QVector<bool> graphs);
};

#endif // UIFACTORY_H
