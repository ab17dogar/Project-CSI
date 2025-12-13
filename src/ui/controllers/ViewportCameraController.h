#ifndef RAYTRACER_VIEWPORT_CAMERA_CONTROLLER_H
#define RAYTRACER_VIEWPORT_CAMERA_CONTROLLER_H

#include <QObject>
#include <QVector3D>
#include <QMatrix4x4>

namespace Raytracer {
namespace scene {

/**
 * @brief Camera controller for 3D viewport navigation
 * 
 * Provides orbit, pan, and zoom controls for navigating the 3D viewport.
 * Uses a spherical coordinate system centered on a target point.
 */
class ViewportCameraController : public QObject {
    Q_OBJECT
    
public:
    explicit ViewportCameraController(QObject *parent = nullptr);
    ~ViewportCameraController() override = default;
    
    // View matrix
    QMatrix4x4 viewMatrix() const;
    
    // Camera position and orientation
    QVector3D position() const;
    QVector3D target() const { return m_target; }
    QVector3D forward() const;
    QVector3D right() const;
    QVector3D up() const;
    
    // Spherical coordinates
    float distance() const { return m_distance; }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    
    // Setters
    void setTarget(const QVector3D &target);
    void setDistance(float distance);
    void setYaw(float yaw);
    void setPitch(float pitch);
    
    // Navigation controls
    void orbit(float deltaYaw, float deltaPitch);
    void pan(float deltaX, float deltaY);
    void zoom(float delta);
    void dolly(float delta);
    
    // Framing
    void framePoints(const QVector<QVector3D> &points, float padding = 1.2f);
    void frameBoundingBox(const QVector3D &min, const QVector3D &max, float padding = 1.2f);
    
    // Reset
    void reset();
    
    // Constraints
    void setMinDistance(float dist) { m_minDistance = dist; }
    void setMaxDistance(float dist) { m_maxDistance = dist; }
    void setMinPitch(float pitch) { m_minPitch = pitch; }
    void setMaxPitch(float pitch) { m_maxPitch = pitch; }
    
    float minDistance() const { return m_minDistance; }
    float maxDistance() const { return m_maxDistance; }
    float minPitch() const { return m_minPitch; }
    float maxPitch() const { return m_maxPitch; }
    
    // Speed controls
    void setOrbitSpeed(float speed) { m_orbitSpeed = speed; }
    void setPanSpeed(float speed) { m_panSpeed = speed; }
    void setZoomSpeed(float speed) { m_zoomSpeed = speed; }
    
signals:
    void cameraChanged();
    
private:
    void clampValues();
    void updateFromSpherical();
    
private:
    // Target point (orbit center)
    QVector3D m_target = QVector3D(0, 0, 0);
    
    // Spherical coordinates relative to target
    float m_distance = 10.0f;
    float m_yaw = 45.0f;     // Horizontal angle in degrees
    float m_pitch = -30.0f;  // Vertical angle in degrees
    
    // Constraints
    float m_minDistance = 0.1f;
    float m_maxDistance = 1000.0f;
    float m_minPitch = -89.0f;
    float m_maxPitch = 89.0f;
    
    // Speed modifiers
    float m_orbitSpeed = 1.0f;
    float m_panSpeed = 1.0f;
    float m_zoomSpeed = 1.0f;
    
    // Default values for reset
    QVector3D m_defaultTarget = QVector3D(0, 0, 0);
    float m_defaultDistance = 10.0f;
    float m_defaultYaw = 45.0f;
    float m_defaultPitch = -30.0f;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_VIEWPORT_CAMERA_CONTROLLER_H
