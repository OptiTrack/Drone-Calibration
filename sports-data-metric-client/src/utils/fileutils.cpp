#include "fileutils.h"

QString loadStyleSheet(const QString &fileName) {
    QFile file(fileName);

    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "Could not open style sheet:" << fileName;
        return "";
    }
    return QString::fromUtf8(file.readAll());
}

QJsonObject loadJSON(const QString &fileName) {
    QFile file(fileName);

    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "Could not open JSON file:" << fileName;
        return QJsonObject();
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << parseError.errorString();
        return QJsonObject();
    }

    if (!doc.isObject()) {
        return QJsonObject();
    }

    return doc.object();
}

QList<QJsonObject> parseSportObjects(const QJsonObject &sportsFile) {
    QList<QJsonObject> sportObjects;

    if (!sportsFile.contains("sports") || !sportsFile["sports"].isArray()) {
        return sportObjects;
    }

    QJsonArray sportsArray = sportsFile["sports"].toArray();
    for (const QJsonValue &sportValue : sportsArray) {
        if (sportValue.isObject()) {
            sportObjects.append(sportValue.toObject());
        }
    }
    return sportObjects;
}

QStringList parseSportTypes(const QJsonObject &sportsFile)
{
    QStringList sportTypes;
    QList<QJsonObject> sportObjects = parseSportObjects(sportsFile);

    for (const QJsonObject &sport : sportObjects) {
        if (sport.contains("name") && sport["name"].isString()) {
            sportTypes.append(sport["name"].toString());
        }
    }

    return sportTypes;
};

QJsonArray parseSportMetricSettings(const QJsonObject &sportsFile, QString sportName, QString metricType)
{
    QList<QJsonObject> sportObjects = parseSportObjects(sportsFile);

    for (const QJsonObject &sport : sportObjects) {
        if (sport.contains("name") && sport["name"].toString() == sportName) {
            if (sport.contains(metricType) && sport[metricType].isArray()) {
                return sport[metricType].toArray();
            }
        }
    }

    return QJsonArray();
};

QStringList fetchResourceFileNames(const QString& pathPrefix)
{
    QStringList fileNames;
    QDir resourceDirectory(":" + pathPrefix);

    if (!resourceDirectory.exists()) {
        qWarning() << "Resource directory does not exist:" << resourceDirectory.path();
        return fileNames;
    }

    QStringList entries = resourceDirectory.entryList(QDir::Files);
    for (const QString &entry: entries) {
        fileNames.append(entry);
    }

    return fileNames;
}

QStringList fetchSavedTakeFileNames()
{
    QStringList fileNames;
    QString savedTakesPath = QCoreApplication::applicationDirPath() + "/saved_takes/";

    QDir dir(savedTakesPath);
    if (!dir.exists()) {
        qWarning() << "Saved take directory does not exist:" << savedTakesPath;
        return fileNames;
    }

    QStringList filters;
    filters << "*.json";
    dir.setNameFilters(filters);

    fileNames = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    return fileNames;
}
