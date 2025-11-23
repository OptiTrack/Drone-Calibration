#ifndef WAYPOINT_H
#define WAYPOINT_H

#include <QVector3D>
#include <QJsonObject>

/**
 * @brief Waypoint model for path planning
 * 
 * Represents a single waypoint in 3D space with optional parameters
 * for drone navigation. The coordinate system follows the existing
 * OpenGL convention: X-right, Y-up, Z-forward (right-handed).
 */
struct Waypoint
{
    int id;              ///< 1-based index in the path sequence
    float x;             ///< X coordinate in world units
    float y;             ///< Y coordinate (altitude) in world units
    float z;             ///< Z coordinate in world units
    
    // Optional parameters for advanced path planning
    float yaw;           ///< Heading angle in degrees (0 = forward along +Z)
    float speed;         ///< Target speed in m/s (0 = use default)
    float holdTime;      ///< Time to hover at waypoint in seconds (0 = no hold)
    
    /**
     * @brief Default constructor
     */
    Waypoint()
        : id(0), x(0.0f), y(0.0f), z(0.0f)
        , yaw(0.0f), speed(0.0f), holdTime(0.0f)
    {}
    
    /**
     * @brief Construct waypoint from position
     * @param id Waypoint ID (1-based)
     * @param position 3D position vector
     */
    Waypoint(int id, const QVector3D& position)
        : id(id), x(position.x()), y(position.y()), z(position.z())
        , yaw(0.0f), speed(0.0f), holdTime(0.0f)
    {}
    
    /**
     * @brief Construct waypoint with all parameters
     */
    Waypoint(int id, float x, float y, float z, float yaw = 0.0f, 
             float speed = 0.0f, float holdTime = 0.0f)
        : id(id), x(x), y(y), z(z)
        , yaw(yaw), speed(speed), holdTime(holdTime)
    {}
    
    /**
     * @brief Get position as QVector3D
     */
    QVector3D position() const { return QVector3D(x, y, z); }
    
    /**
     * @brief Set position from QVector3D
     */
    void setPosition(const QVector3D& pos) 
    { 
        x = pos.x(); 
        y = pos.y(); 
        z = pos.z(); 
    }
    
    /**
     * @brief Serialize waypoint to JSON
     */
    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["id"] = id;
        obj["x"] = static_cast<double>(x);
        obj["y"] = static_cast<double>(y);
        obj["z"] = static_cast<double>(z);
        
        // Only include optional fields if they're non-zero
        if (yaw != 0.0f) obj["yaw"] = static_cast<double>(yaw);
        if (speed != 0.0f) obj["speed"] = static_cast<double>(speed);
        if (holdTime != 0.0f) obj["holdTime"] = static_cast<double>(holdTime);
        
        return obj;
    }
    
    /**
     * @brief Deserialize waypoint from JSON
     */
    static Waypoint fromJson(const QJsonObject& obj)
    {
        Waypoint wp;
        wp.id = obj["id"].toInt();
        wp.x = static_cast<float>(obj["x"].toDouble());
        wp.y = static_cast<float>(obj["y"].toDouble());
        wp.z = static_cast<float>(obj["z"].toDouble());
        wp.yaw = static_cast<float>(obj["yaw"].toDouble(0.0));
        wp.speed = static_cast<float>(obj["speed"].toDouble(0.0));
        wp.holdTime = static_cast<float>(obj["holdTime"].toDouble(0.0));
        return wp;
    }
};

#endif // WAYPOINT_H

