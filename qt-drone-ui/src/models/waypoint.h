#ifndef WAYPOINT_H
#define WAYPOINT_H

#include <QVector3D>
#include <QString>
#include <QJsonObject>
#include <QDateTime>

class Waypoint
{
public:
    Waypoint();
    Waypoint(const QVector3D &position, const QString &name = "");
    Waypoint(float x, float y, float z, const QString &name = "");
    
    // Getters
    QVector3D position() const { return m_position; }
    float x() const { return m_position.x(); }
    float y() const { return m_position.y(); }
    float z() const { return m_position.z(); }
    QString name() const { return m_name; }
    QString description() const { return m_description; }
    int sequence() const { return m_sequence; }
    QString waypointType() const { return m_waypointType; }
    float acceptanceRadius() const { return m_acceptanceRadius; }
    float holdTime() const { return m_holdTime; }
    float yawAngle() const { return m_yawAngle; }
    bool passThrough() const { return m_passThrough; }
    QDateTime createdAt() const { return m_createdAt; }
    
    // Setters
    void setPosition(const QVector3D &position) { m_position = position; }
    void setPosition(float x, float y, float z) { m_position = QVector3D(x, y, z); }
    void setX(float x) { m_position.setX(x); }
    void setY(float y) { m_position.setY(y); }
    void setZ(float z) { m_position.setZ(z); }
    void setName(const QString &name) { m_name = name; }
    void setDescription(const QString &description) { m_description = description; }
    void setSequence(int sequence) { m_sequence = sequence; }
    void setWaypointType(const QString &type) { m_waypointType = type; }
    void setAcceptanceRadius(float radius) { m_acceptanceRadius = radius; }
    void setHoldTime(float time) { m_holdTime = time; }
    void setYawAngle(float angle) { m_yawAngle = angle; }
    void setPassThrough(bool passThrough) { m_passThrough = passThrough; }
    
    // Utility functions
    float distanceTo(const Waypoint &other) const;
    float distanceTo(const QVector3D &position) const;
    QJsonObject toJson() const;
    static Waypoint fromJson(const QJsonObject &json);
    QString toString() const;
    
    // Operators
    bool operator==(const Waypoint &other) const;
    bool operator!=(const Waypoint &other) const;
    
private:
    QVector3D m_position;
    QString m_name;
    QString m_description;
    int m_sequence;
    QString m_waypointType; // NAV_WAYPOINT, LOITER_UNLIM, LAND, etc.
    float m_acceptanceRadius;
    float m_holdTime;
    float m_yawAngle;
    bool m_passThrough;
    QDateTime m_createdAt;
};

Q_DECLARE_METATYPE(Waypoint)

#endif // WAYPOINT_H