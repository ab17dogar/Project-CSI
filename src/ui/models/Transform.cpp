#include "Transform.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace Raytracer {
namespace scene {

Transform::Transform(QObject *parent)
    : QObject(parent)
{
}

Transform::Transform(const glm::vec3 &position, const glm::vec3 &rotation, const glm::vec3 &scale, QObject *parent)
    : QObject(parent)
    , m_position(position)
    , m_rotation(rotation)
    , m_scale(scale)
{
}

void Transform::setPosition(const glm::vec3 &pos)
{
    if (m_position != pos) {
        m_position = pos;
        markDirty();
        emit positionChanged(pos);
        emit changed();
    }
}

void Transform::translate(const glm::vec3 &delta)
{
    setPosition(m_position + delta);
}

void Transform::setRotation(const glm::vec3 &rot)
{
    if (m_rotation != rot) {
        m_rotation = rot;
        markDirty();
        emit rotationChanged(rot);
        emit changed();
    }
}

void Transform::rotate(const glm::vec3 &delta)
{
    setRotation(m_rotation + delta);
}

glm::quat Transform::quaternion() const
{
    // Convert Euler angles (degrees) to quaternion
    glm::vec3 radians = glm::radians(m_rotation);
    return glm::quat(radians);
}

void Transform::setQuaternion(const glm::quat &quat)
{
    // Convert quaternion to Euler angles (degrees)
    glm::vec3 euler = glm::degrees(glm::eulerAngles(quat));
    setRotation(euler);
}

void Transform::setScale(const glm::vec3 &scl)
{
    if (m_scale != scl) {
        m_scale = scl;
        markDirty();
        emit scaleChanged(scl);
        emit changed();
    }
}

glm::mat4 Transform::localMatrix() const
{
    if (m_matrixDirty) {
        updateMatrixCache();
    }
    return m_localMatrix;
}

glm::mat4 Transform::worldMatrix() const
{
    if (m_matrixDirty) {
        updateMatrixCache();
    }
    return m_worldMatrix;
}

void Transform::setWorldMatrix(const glm::mat4 &worldMat)
{
    // Decompose world matrix and apply (simplified - doesn't handle parent)
    m_position = glm::vec3(worldMat[3]);
    
    // Extract scale from matrix columns
    m_scale.x = glm::length(glm::vec3(worldMat[0]));
    m_scale.y = glm::length(glm::vec3(worldMat[1]));
    m_scale.z = glm::length(glm::vec3(worldMat[2]));
    
    // Extract rotation matrix (remove scale)
    glm::mat3 rotMat;
    rotMat[0] = glm::vec3(worldMat[0]) / m_scale.x;
    rotMat[1] = glm::vec3(worldMat[1]) / m_scale.y;
    rotMat[2] = glm::vec3(worldMat[2]) / m_scale.z;
    
    // Convert rotation matrix to Euler angles
    glm::quat quat = glm::quat_cast(rotMat);
    m_rotation = glm::degrees(glm::eulerAngles(quat));
    
    markDirty();
    emit changed();
}

void Transform::setParentTransform(Transform *parent)
{
    if (m_parentTransform != parent) {
        m_parentTransform = parent;
        markDirty();
        emit changed();
    }
}

glm::vec3 Transform::forward() const
{
    glm::mat4 world = worldMatrix();
    return -glm::normalize(glm::vec3(world[2])); // -Z is forward
}

glm::vec3 Transform::right() const
{
    glm::mat4 world = worldMatrix();
    return glm::normalize(glm::vec3(world[0])); // +X is right
}

glm::vec3 Transform::up() const
{
    glm::mat4 world = worldMatrix();
    return glm::normalize(glm::vec3(world[1])); // +Y is up
}

void Transform::reset()
{
    m_position = glm::vec3(0.0f);
    m_rotation = glm::vec3(0.0f);
    m_scale = glm::vec3(1.0f);
    markDirty();
    emit changed();
}

void Transform::copyFrom(const Transform &other)
{
    m_position = other.m_position;
    m_rotation = other.m_rotation;
    m_scale = other.m_scale;
    markDirty();
    emit changed();
}

void Transform::markDirty()
{
    m_matrixDirty = true;
}

void Transform::updateMatrixCache() const
{
    // Build local matrix: T * R * S
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_position);
    
    // Rotation using Euler angles (Y * X * Z order, common for 3D)
    glm::vec3 radians = glm::radians(m_rotation);
    glm::mat4 rotation = glm::eulerAngleYXZ(radians.y, radians.x, radians.z);
    
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), m_scale);
    
    m_localMatrix = translation * rotation * scale;
    
    // Compute world matrix
    if (m_parentTransform) {
        m_worldMatrix = m_parentTransform->worldMatrix() * m_localMatrix;
    } else {
        m_worldMatrix = m_localMatrix;
    }
    
    m_matrixDirty = false;
}

} // namespace scene
} // namespace Raytracer
