#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <QFile>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QStringList>
#include <QCoreApplication>

QString loadStyleSheet(const QString &fileName);
QJsonObject loadJSON(const QString &fileName);

QList<QJsonObject> parseSportObjects(const QJsonObject &sportsFile);
QStringList parseSportTypes(const QJsonObject &sportsFile);
QJsonArray parseSportMetricSettings(const QJsonObject &sportsFile, QString sportName, QString metricType);
QStringList fetchResourceFileNames(const QString& pathPrefix);
QStringList fetchSavedTakeFileNames();

#endif // FILEUTILS_H
