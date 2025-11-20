#ifndef FLIGHTPATH_H
#define FLIGHTPATH_H

#include "waypoint.h"
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <QUuid>
#include <QDateTime>

class FlightPath
{
public:
    FlightPath();
    FlightPath(const QString &name);
    
    // Getters
    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString description() const { return m_description; }
    QVector<Waypoint> waypoints() const { return m_waypoints; }
    int waypointCount() const { return m_waypoints.size(); }
    float totalDistance() const;
    float estimatedFlightTime(float averageSpeed = 5.0f) const;
    QDateTime createdAt() const { return m_createdAt; }
    QDateTime modifiedAt() const { return m_modifiedAt; }
    bool isValid() const;
    
    // Setters
    void setName(const QString &name);
    void setDescription(const QString &description);
    void setWaypoints(const QVector<Waypoint> &waypoints);
    
    // Waypoint management
    void addWaypoint(const Waypoint &waypoint);
    void addWaypoint(const QVector3D &position, const QString &name = "");
    void insertWaypoint(int index, const Waypoint &waypoint);
    void removeWaypoint(int index);
    void removeWaypoint(const Waypoint &waypoint);
    void clearWaypoints();
    void moveWaypoint(int fromIndex, int toIndex);
    
    // Access
    Waypoint& waypoint(int index);
    const Waypoint& waypoint(int index) const;
    Waypoint& operator[](int index);
    const Waypoint& operator[](int index) const;
    
    // Utility functions
    QJsonObject toJson() const;
    static FlightPath fromJson(const QJsonObject &json);
    void updateSequences();
    FlightPath reversed() const;
    FlightPath optimized() const; // Simple TSP optimization
    
    // Operators
    bool operator==(const FlightPath &other) const;
    bool operator!=(const FlightPath &other) const;
    
private:
    void updateModificationTime();
    QString generateNewId();
    
    QString m_id;
    QString m_name;
    QString m_description;
    QVector<Waypoint> m_waypoints;
    QDateTime m_createdAt;
    QDateTime m_modifiedAt;
};

Q_DECLARE_METATYPE(FlightPath)

#endif // FLIGHTPATH_H