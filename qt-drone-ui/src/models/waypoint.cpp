#include "waypoint.h"
#include <QJsonArray>
#include <cmath>

Waypoint::Waypoint()
    : m_position(0, 0, 0)
    , m_latitude(0.0)
    , m_longitude(0.0)
    , m_relativeAltitudeM(0.0f)
    , m_sequence(0)
    , m_waypointType("NAV_WAYPOINT")
    , m_acceptanceRadius(1.0f)
    , m_holdTime(0.0f)
    , m_yawAngle(0.0f)
    , m_passThrough(false)
    , m_createdAt(QDateTime::currentDateTime())
{
}

Waypoint::Waypoint(const QVector3D &position, const QString &name)
    : m_position(position)
    , m_latitude(0.0)
    , m_longitude(0.0)
    , m_relativeAltitudeM(position.z())
    , m_name(name)
    , m_sequence(0)
    , m_waypointType("NAV_WAYPOINT")
    , m_acceptanceRadius(1.0f)
    , m_holdTime(0.0f)
    , m_yawAngle(0.0f)
    , m_passThrough(false)
    , m_createdAt(QDateTime::currentDateTime())
{
}

Waypoint::Waypoint(float x, float y, float z, const QString &name)
    : m_position(x, y, z)
    , m_latitude(0.0)
    , m_longitude(0.0)
    , m_relativeAltitudeM(z)
    , m_name(name)
    , m_sequence(0)
    , m_waypointType("NAV_WAYPOINT")
    , m_acceptanceRadius(1.0f)
    , m_holdTime(0.0f)
    , m_yawAngle(0.0f)
    , m_passThrough(false)
    , m_createdAt(QDateTime::currentDateTime())
{
}

float Waypoint::distanceTo(const Waypoint &other) const
{
    return m_position.distanceToPoint(other.m_position);
}

float Waypoint::distanceTo(const QVector3D &position) const
{
    return m_position.distanceToPoint(position);
}

QJsonObject Waypoint::toJson() const
{
    QJsonObject json;
    
    // For VOXL2/PX4 mission format
    json["lat"] = m_latitude;
    json["lon"] = m_longitude;
    json["rel_alt_m"] = static_cast<double>(m_relativeAltitudeM);
    json["hold_s"] = static_cast<double>(m_holdTime);
    json["yaw_deg"] = static_cast<double>(m_yawAngle);
    
    // Additional metadata (optional for mission, useful for UI)
    if (!m_name.isEmpty()) {
        json["name"] = m_name;
    }
    if (!m_description.isEmpty()) {
        json["description"] = m_description;
    }
    json["sequence"] = m_sequence;
    json["type"] = m_waypointType;
    json["acceptance_radius"] = static_cast<double>(m_acceptanceRadius);
    json["pass_through"] = m_passThrough;
    
    // Local position for UI
    QJsonObject posObj;
    posObj["x"] = static_cast<double>(m_position.x());
    posObj["y"] = static_cast<double>(m_position.y());
    posObj["z"] = static_cast<double>(m_position.z());
    json["local_position"] = posObj;
    
    return json;
}

Waypoint Waypoint::fromJson(const QJsonObject &json)
{
    Waypoint wp;
    
    // GPS coordinates
    wp.m_latitude = json["lat"].toDouble();
    wp.m_longitude = json["lon"].toDouble();
    wp.m_relativeAltitudeM = static_cast<float>(json["rel_alt_m"].toDouble());
    wp.m_holdTime = static_cast<float>(json["hold_s"].toDouble(0.0));
    wp.m_yawAngle = static_cast<float>(json["yaw_deg"].toDouble(0.0));
    
    // Metadata
    wp.m_name = json["name"].toString();
    wp.m_description = json["description"].toString();
    wp.m_sequence = json["sequence"].toInt(0);
    wp.m_waypointType = json["type"].toString("NAV_WAYPOINT");
    wp.m_acceptanceRadius = static_cast<float>(json["acceptance_radius"].toDouble(1.0));
    wp.m_passThrough = json["pass_through"].toBool(false);
    
    // Local position
    if (json.contains("local_position")) {
        QJsonObject posObj = json["local_position"].toObject();
        wp.m_position = QVector3D(
            static_cast<float>(posObj["x"].toDouble()),
            static_cast<float>(posObj["y"].toDouble()),
            static_cast<float>(posObj["z"].toDouble())
        );
    }
    
    return wp;
}

QString Waypoint::toString() const
{
    return QString("Waypoint(%1: lat=%2, lon=%3, alt=%4m)")
        .arg(m_name.isEmpty() ? QString::number(m_sequence) : m_name)
        .arg(m_latitude, 0, 'f', 6)
        .arg(m_longitude, 0, 'f', 6)
        .arg(m_relativeAltitudeM, 0, 'f', 1);
}

bool Waypoint::operator==(const Waypoint &other) const
{
    constexpr float kPosEps = 1e-5f;
    constexpr float kFloatEps = 1e-5f;
    constexpr double kDoubleEps = 1e-9;

    return m_sequence == other.m_sequence &&
           m_name == other.m_name &&
           m_description == other.m_description &&
           m_waypointType == other.m_waypointType &&
           m_passThrough == other.m_passThrough &&
           std::abs(m_position.x() - other.m_position.x()) < kPosEps &&
           std::abs(m_position.y() - other.m_position.y()) < kPosEps &&
           std::abs(m_position.z() - other.m_position.z()) < kPosEps &&
           std::abs(m_latitude - other.m_latitude) < kDoubleEps &&
           std::abs(m_longitude - other.m_longitude) < kDoubleEps &&
           std::abs(m_relativeAltitudeM - other.m_relativeAltitudeM) < kFloatEps &&
           std::abs(m_acceptanceRadius - other.m_acceptanceRadius) < kFloatEps &&
           std::abs(m_holdTime - other.m_holdTime) < kFloatEps &&
           std::abs(m_yawAngle - other.m_yawAngle) < kFloatEps;
}

bool Waypoint::operator!=(const Waypoint &other) const
{
    return !(*this == other);
}