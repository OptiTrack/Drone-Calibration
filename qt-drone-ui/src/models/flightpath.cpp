#include "flightpath.h"
#include <QJsonArray>
#include <QRegularExpression>

QString FlightPath::fileBaseFromDisplayName(const QString &displayName)
{
    QString s = displayName.trimmed();
    s.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_\\-\\s]")), QString());
    s.replace(QLatin1Char(' '), QLatin1Char('_'));
    if (s.isEmpty())
        s = QStringLiteral("path");
    return s;
}

QString FlightPath::displayNameFromFileBase(const QString &fileBase)
{
    QString s = fileBase;
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    return s;
}

FlightPath::FlightPath()
    : m_id(generateNewId())
    , m_name("New Flight Path")
    , m_createdAt(QDateTime::currentDateTime())
    , m_modifiedAt(QDateTime::currentDateTime())
    , m_homeRelative(true)
    , m_takeoffAltM(3.0f)
    , m_cruiseSpeedMS(2.0f)
    , m_autoLand(true)
{
}

FlightPath::FlightPath(const QString &name)
    : m_id(generateNewId())
    , m_name(name)
    , m_createdAt(QDateTime::currentDateTime())
    , m_modifiedAt(QDateTime::currentDateTime())
    , m_homeRelative(true)
    , m_takeoffAltM(3.0f)
    , m_cruiseSpeedMS(2.0f)
    , m_autoLand(true)
{
}

void FlightPath::setName(const QString &name)
{
    m_name = name;
    updateModificationTime();
}

void FlightPath::setDescription(const QString &description)
{
    m_description = description;
    updateModificationTime();
}

void FlightPath::setCreatedAt(const QDateTime &dt)
{
    m_createdAt = dt;
}

void FlightPath::setModifiedAt(const QDateTime &dt)
{
    m_modifiedAt = dt;
}

void FlightPath::setWaypoints(const QVector<Waypoint> &waypoints)
{
    m_waypoints = waypoints;
    updateSequences();
    updateModificationTime();
}

void FlightPath::addWaypoint(const Waypoint &waypoint)
{
    m_waypoints.append(waypoint);
    updateSequences();
    updateModificationTime();
}

void FlightPath::addWaypoint(const QVector3D &position, const QString &name)
{
    Waypoint wp(position, name);
    addWaypoint(wp);
}

void FlightPath::insertWaypoint(int index, const Waypoint &waypoint)
{
    if (index >= 0 && index <= m_waypoints.size()) {
        m_waypoints.insert(index, waypoint);
        updateSequences();
        updateModificationTime();
    }
}

void FlightPath::removeWaypoint(int index)
{
    if (index >= 0 && index < m_waypoints.size()) {
        m_waypoints.remove(index);
        updateSequences();
        updateModificationTime();
    }
}

void FlightPath::removeWaypoint(const Waypoint &waypoint)
{
    int index = m_waypoints.indexOf(waypoint);
    if (index != -1) {
        removeWaypoint(index);
    }
}

void FlightPath::clearWaypoints()
{
    m_waypoints.clear();
    updateModificationTime();
}

void FlightPath::moveWaypoint(int fromIndex, int toIndex)
{
    if (fromIndex >= 0 && fromIndex < m_waypoints.size() &&
        toIndex >= 0 && toIndex < m_waypoints.size() &&
        fromIndex != toIndex) {
        m_waypoints.move(fromIndex, toIndex);
        updateSequences();
        updateModificationTime();
    }
}

Waypoint& FlightPath::waypoint(int index)
{
    return m_waypoints[index];
}

const Waypoint& FlightPath::waypoint(int index) const
{
    return m_waypoints[index];
}

Waypoint& FlightPath::operator[](int index)
{
    return m_waypoints[index];
}

const Waypoint& FlightPath::operator[](int index) const
{
    return m_waypoints[index];
}

float FlightPath::totalDistance() const
{
    if (m_waypoints.size() < 2) {
        return 0.0f;
    }
    
    float distance = 0.0f;
    for (int i = 0; i < m_waypoints.size() - 1; ++i) {
        distance += m_waypoints[i].distanceTo(m_waypoints[i + 1]);
    }
    return distance;
}

float FlightPath::estimatedFlightTime(float averageSpeed) const
{
    if (averageSpeed <= 0.0f || m_waypoints.isEmpty()) {
        return 0.0f;
    }
    
    float time = totalDistance() / averageSpeed;
    
    // Add hold times
    for (const Waypoint &wp : m_waypoints) {
        time += wp.holdTime();
    }
    
    return time;
}

bool FlightPath::isValid() const
{
    // Mission needs at least 1 waypoint
    if (m_waypoints.isEmpty()) {
        return false;
    }
    
    // Check that all waypoints have valid GPS coordinates
    for (const Waypoint &wp : m_waypoints) {
        if (wp.latitude() == 0.0 && wp.longitude() == 0.0) {
            return false;  // Likely unset coordinates
        }
    }
    
    return true;
}

QJsonObject FlightPath::toJson() const
{
    QJsonObject json;
    
    // Mission parameters for VOXL2/PX4
    json["home_relative"] = m_homeRelative;
    json["takeoff_alt_m"] = static_cast<double>(m_takeoffAltM);
    json["cruise_speed_m_s"] = static_cast<double>(m_cruiseSpeedMS);
    json["land"] = m_autoLand;
    
    // Waypoints array
    QJsonArray waypointsArray;
    for (const Waypoint &wp : m_waypoints) {
        waypointsArray.append(wp.toJson());
    }
    json["waypoints"] = waypointsArray;
    
    // Metadata (for UI/tracking, not mission-critical)
    json["id"] = m_id;
    json["name"] = m_name;
    json["description"] = m_description;
    json["created_at"] = m_createdAt.toString(Qt::ISODate);
    json["modified_at"] = m_modifiedAt.toString(Qt::ISODate);
    
    return json;
}

FlightPath FlightPath::fromJson(const QJsonObject &json)
{
    FlightPath path;
    
    // Mission parameters
    path.m_homeRelative = json["home_relative"].toBool(true);
    path.m_takeoffAltM = static_cast<float>(json["takeoff_alt_m"].toDouble(3.0));
    path.m_cruiseSpeedMS = static_cast<float>(json["cruise_speed_m_s"].toDouble(2.0));
    path.m_autoLand = json["land"].toBool(true);
    
    // Waypoints
    QJsonArray waypointsArray = json["waypoints"].toArray();
    for (const QJsonValue &value : waypointsArray) {
        path.m_waypoints.append(Waypoint::fromJson(value.toObject()));
    }
    path.updateSequences();
    
    // Metadata
    if (json.contains("id")) {
        path.m_id = json["id"].toString();
    }
    if (json.contains("name")) {
        path.m_name = json["name"].toString();
    }
    if (json.contains("description")) {
        path.m_description = json["description"].toString();
    }
    if (json.contains("created_at")) {
        path.m_createdAt = QDateTime::fromString(json["created_at"].toString(), Qt::ISODate);
    }
    if (json.contains("modified_at")) {
        path.m_modifiedAt = QDateTime::fromString(json["modified_at"].toString(), Qt::ISODate);
    }
    
    return path;
}

void FlightPath::updateSequences()
{
    for (int i = 0; i < m_waypoints.size(); ++i) {
        m_waypoints[i].setSequence(i);
    }
}

FlightPath FlightPath::reversed() const
{
    FlightPath reversed;
    reversed.m_name = m_name + " (Reversed)";
    reversed.m_description = m_description;
    reversed.m_homeRelative = m_homeRelative;
    reversed.m_takeoffAltM = m_takeoffAltM;
    reversed.m_cruiseSpeedMS = m_cruiseSpeedMS;
    reversed.m_autoLand = m_autoLand;
    
    for (int i = m_waypoints.size() - 1; i >= 0; --i) {
        reversed.m_waypoints.append(m_waypoints[i]);
    }
    reversed.updateSequences();
    
    return reversed;
}

FlightPath FlightPath::optimized() const
{
    // Simple nearest-neighbor TSP optimization
    // For production, use more sophisticated algorithms
    if (m_waypoints.size() <= 2) {
        return *this;
    }
    
    FlightPath optimized;
    optimized.m_name = m_name + " (Optimized)";
    optimized.m_description = m_description;
    optimized.m_homeRelative = m_homeRelative;
    optimized.m_takeoffAltM = m_takeoffAltM;
    optimized.m_cruiseSpeedMS = m_cruiseSpeedMS;
    optimized.m_autoLand = m_autoLand;
    
    QVector<Waypoint> remaining = m_waypoints;
    optimized.m_waypoints.append(remaining.takeFirst());
    
    while (!remaining.isEmpty()) {
        const Waypoint &current = optimized.m_waypoints.last();
        int nearestIndex = 0;
        float nearestDistance = current.distanceTo(remaining[0]);
        
        for (int i = 1; i < remaining.size(); ++i) {
            float distance = current.distanceTo(remaining[i]);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = i;
            }
        }
        
        optimized.m_waypoints.append(remaining.takeAt(nearestIndex));
    }
    
    optimized.updateSequences();
    return optimized;
}

bool FlightPath::operator==(const FlightPath &other) const
{
    return m_id == other.m_id;
}

bool FlightPath::operator!=(const FlightPath &other) const
{
    return !(*this == other);
}

void FlightPath::updateModificationTime()
{
    m_modifiedAt = QDateTime::currentDateTime();
}

QString FlightPath::generateNewId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}