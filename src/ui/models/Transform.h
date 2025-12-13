#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <QObject>

namespace Raytracer {
namespace scene {

/**
 * @brief 3D Transform component with position, rotation, and scale.
 * 
 * Supports both Euler angles and quaternion rotation internally.
 * Computes local and world transformation matrices.
 */
class Transform : public QObject
{
    Q_OBJECT

public:
    explicit Transform(QObject *parent = nullptr);
    Transform(const glm::vec3 &position, const glm::vec3 &rotation, const glm::vec3 &scale, QObject *parent = nullptr);
    
    // Position
    glm::vec3 position() const { return m_position; }
    void setPosition(const glm::vec3 &pos);
    void setPosition(float x, float y, float z) { setPosition(glm::vec3(x, y, z)); }
    void translate(const glm::vec3 &delta);
    
    // Rotation (Euler angles in degrees)
    glm::vec3 rotation() const { return m_rotation; }
    void setRotation(const glm::vec3 &rot);
    void setRotation(float x, float y, float z) { setRotation(glm::vec3(x, y, z)); }
    void rotate(const glm::vec3 &delta);
    
    // Rotation (Quaternion)
    glm::quat quaternion() const;
    void setQuaternion(const glm::quat &quat);
    
    // Scale
    glm::vec3 scale() const { return m_scale; }
    void setScale(const glm::vec3 &scl);
    void setScale(float x, float y, float z) { setScale(glm::vec3(x, y, z)); }
    void setUniformScale(float s) { setScale(glm::vec3(s, s, s)); }
    
    // Matrices
    glm::mat4 localMatrix() const;
    glm::mat4 worldMatrix() const;
    void setWorldMatrix(const glm::mat4 &worldMat);
    
    // Parent transform (for hierarchical transforms)
    Transform *parentTransform() const { return m_parentTransform; }
    void setParentTransform(Transform *parent);
    
    // Direction vectors (in world space)
    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
    
    // Reset to identity
    void reset();
    
    // Copy from another transform
    void copyFrom(const Transform &other);

signals:
    void changed();
    void positionChanged(const glm::vec3 &newPos);
    void rotationChanged(const glm::vec3 &newRot);
    void scaleChanged(const glm::vec3 &newScale);

private:
    void markDirty();
    void updateMatrixCache() const;
    
    glm::vec3 m_position {0.0f, 0.0f, 0.0f};
    glm::vec3 m_rotation {0.0f, 0.0f, 0.0f};  // Euler angles in degrees
    glm::vec3 m_scale {1.0f, 1.0f, 1.0f};
    
    Transform *m_parentTransform {nullptr};
    
    // Cached matrices (mutable for lazy evaluation)
    mutable glm::mat4 m_localMatrix {1.0f};
    mutable glm::mat4 m_worldMatrix {1.0f};
    mutable bool m_matrixDirty {true};
};

} // namespace scene
} // namespace Raytracer
