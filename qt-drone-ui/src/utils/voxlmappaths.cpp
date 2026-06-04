#include "voxlmappaths.h"

#include <QRegularExpression>

namespace {
QString stripSpaces(const QString &value)
{
    QString out = value;
    out.remove(QLatin1Char(' '));
    return out;
}

QString trimTrailingSlashes(QString value)
{
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);
    return value;
}

QString shellSingleQuote(QString value)
{
    value.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(value);
}
}

QString VoxlMapperPaths::roomSubdir(const QString &roomBaseName)
{
    const QString base = stripSpaces(roomBaseName.trimmed());
    if (base.isEmpty())
        return QStringLiteral("missions/default/");
    return QStringLiteral("missions/%1/").arg(base);
}

QString VoxlMapperPaths::normalizeSubdir(const QString &storedPath)
{
    QString trimmed = storedPath.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    static const QRegularExpression absoluteHyphen(
        QStringLiteral("^/data/voxl-mapper/?(.*)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression absoluteUnderscore(
        QStringLiteral("^/data/voxl_mapper/?(.*)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression missionsOnly(
        QStringLiteral("^missions/?(.+?)/?$"), QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = absoluteHyphen.match(trimmed);
    if (match.hasMatch())
        trimmed = match.captured(1).trimmed();
    else {
        match = absoluteUnderscore.match(trimmed);
        if (match.hasMatch())
            trimmed = match.captured(1).trimmed();
    }

    match = missionsOnly.match(trimmed);
    if (match.hasMatch())
        return roomSubdir(match.captured(1));

    if (trimmed.startsWith(QLatin1String("missions/"), Qt::CaseInsensitive))
        return trimmed.endsWith(QLatin1Char('/')) ? trimmed : trimmed + QLatin1Char('/');

    return QString();
}

QString VoxlMapperPaths::listMissionRoomsScript()
{
    return QStringLiteral(
        "for base in /data/voxl-mapper/missions /data/voxl_mapper/missions; do "
        "  if [ -d \"$base\" ]; then "
        "    for d in \"$base\"/*; do [ -d \"$d\" ] && basename \"$d\"; done; "
        "    break; "
        "  fi; "
        "done");
}

QString VoxlMapperPaths::listRoomsAndPollMapScript(const QString &customSubdir)
{
    QString cleanSubdir = stripSpaces(customSubdir.trimmed());
    cleanSubdir = trimTrailingSlashes(cleanSubdir);

    return QStringLiteral(
               "sub=%1; "
               "for base in /data/voxl-mapper/missions /data/voxl_mapper/missions; do "
               "  if [ -d \"$base\" ]; then "
               "    for d in \"$base\"/*; do [ -d \"$d\" ] && printf 'ROOM:%s\\n' \"$(basename \"$d\")\"; done; "
               "    break; "
               "  fi; "
               "done; "
               "has_map() { "
               "  d=\"$1/$sub\"; "
               "  ([ -s \"$d/tsdf\" ] || [ -s \"$d/tsdf_map\" ]) && "
               "  ([ -s \"$d/esdf\" ] || [ -s \"$d/esdf_map\" ]); "
               "}; "
               "if has_map /data/voxl-mapper || has_map /data/voxl_mapper; then echo MAP_READY; else echo MAP_WAIT; fi")
        .arg(shellSingleQuote(cleanSubdir));
}
