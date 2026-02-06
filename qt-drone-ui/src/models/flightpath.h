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
    
    // Mission parameters for VOXL2/PX4
    bool homeRelative() const { return m_homeRelative; }
    float takeoffAltM() const { return m_takeoffAltM; }
    float cruiseSpeedMS() const { return m_cruiseSpeedMS; }
    bool autoLand() const { return m_autoLand; }
    
    // Setters
    void setName(const QString &name);
    void setDescription(const QString &description);
    void setWaypoints(const QVector<Waypoint> &waypoints);
    
    // Mission parameter setters
    void setHomeRelative(bool homeRelative) { m_homeRelative = homeRelative; }
    void setTakeoffAltM(float alt) { m_takeoffAltM = alt; }
    void setCruiseSpeedMS(float speed) { m_cruiseSpeedMS = speed; }
    void setAutoLand(bool autoLand) { m_autoLand = autoLand; }
    
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
    
    // Mission parameters for VOXL2/PX4 execution
    bool m_homeRelative;     // Altitude relative to home position
    float m_takeoffAltM;     // Takeoff altitude in meters
    float m_cruiseSpeedMS;   // Cruise speed in m/s
    bool m_autoLand;         // Auto-land at end of mission
};

Q_DECLARE_METATYPE(FlightPath)

#endif // FLIGHTPATH_H