#include "ViewportCameraController.h"
#include <QtMath>
#include <algorithm>

namespace Raytracer {
namespace scene {

ViewportCameraController::ViewportCameraController(QObject *parent)
    : QObject(parent) {
  // Setup animation timer for smooth damping
  m_animationTimer.setInterval(16); // ~60 FPS
  connect(&m_animationTimer, &QTimer::timeout, this,
          &ViewportCameraController::onAnimationTick);
}

QMatrix4x4 ViewportCameraController::viewMatrix() const {
  QMatrix4x4 view;
  view.lookAt(position(), m_target, QVector3D(0, 1, 0));
  return view;
}

QVector3D ViewportCameraController::position() const {
  // Convert spherical to cartesian coordinates
  float yawRad = qDegreesToRadians(m_yaw);
  float pitchRad = qDegreesToRadians(m_pitch);

  float cosP = qCos(pitchRad);
  float sinP = qSin(pitchRad);
  float cosY = qCos(yawRad);
  float sinY = qSin(yawRad);

  QVector3D offset(m_distance * cosP * sinY, -m_distance * sinP,
                   m_distance * cosP * cosY);

  return m_target + offset;
}

QVector3D ViewportCameraController::forward() const {
  return (m_target - position()).normalized();
}

QVector3D ViewportCameraController::right() const {
  return QVector3D::crossProduct(forward(), QVector3D(0, 1, 0)).normalized();
}

QVector3D ViewportCameraController::up() const {
  return QVector3D::crossProduct(right(), forward()).normalized();
}

void ViewportCameraController::setTarget(const QVector3D &target) {
  m_target = target;
  emit cameraChanged();
}

void ViewportCameraController::setDistance(float distance) {
  m_distance = distance;
  clampValues();
  emit cameraChanged();
}

void ViewportCameraController::setYaw(float yaw) {
  m_yaw = yaw;
  // Wrap yaw to 0-360 range
  while (m_yaw < 0)
    m_yaw += 360.0f;
  while (m_yaw >= 360)
    m_yaw -= 360.0f;
  emit cameraChanged();
}

void ViewportCameraController::setPitch(float pitch) {
  m_pitch = pitch;
  clampValues();
  emit cameraChanged();
}

void ViewportCameraController::orbit(float deltaYaw, float deltaPitch) {
  m_yaw += deltaYaw * m_orbitSpeed;
  m_pitch += deltaPitch * m_orbitSpeed;

  // Wrap yaw
  while (m_yaw < 0)
    m_yaw += 360.0f;
  while (m_yaw >= 360)
    m_yaw -= 360.0f;

  clampValues();
  emit cameraChanged();
}

void ViewportCameraController::orbitSmooth(float deltaYaw, float deltaPitch) {
  // Add to velocity for smooth movement
  m_velocityYaw += deltaYaw * m_orbitSpeed;
  m_velocityPitch += deltaPitch * m_orbitSpeed;
  startAnimation();
}

void ViewportCameraController::pan(float deltaX, float deltaY) {
  // Pan in camera-relative space
  QVector3D r = right();
  QVector3D u = up();

  float panScale = m_distance * m_panSpeed * 0.1f;
  QVector3D panOffset = r * (-deltaX * panScale) + u * (deltaY * panScale);

  m_target += panOffset;
  emit cameraChanged();
}

void ViewportCameraController::zoom(float delta) {
  // Exponential zoom for consistent feel at all distances
  float factor = qPow(1.1f, -delta * m_zoomSpeed);
  m_distance *= factor;
  clampValues();
  emit cameraChanged();
}

void ViewportCameraController::zoomSmooth(float delta) {
  // Add to velocity for smooth zoom
  m_velocityZoom += delta * m_zoomSpeed;
  startAnimation();
}

void ViewportCameraController::dolly(float delta) {
  // Linear dolly
  m_distance -= delta * m_zoomSpeed;
  clampValues();
  emit cameraChanged();
}

void ViewportCameraController::flyTo(const QVector3D &newTarget,
                                     float newDistance,
                                     float /* durationMs */) {
  // Simple immediate transition (could be improved with animation)
  m_target = newTarget;
  m_distance = newDistance;
  clampValues();
  emit cameraChanged();
}

void ViewportCameraController::framePoints(const QVector<QVector3D> &points,
                                           float padding) {
  if (points.isEmpty())
    return;

  // Calculate bounding box
  QVector3D minPoint = points[0];
  QVector3D maxPoint = points[0];

  for (const auto &p : points) {
    minPoint.setX(qMin(minPoint.x(), p.x()));
    minPoint.setY(qMin(minPoint.y(), p.y()));
    minPoint.setZ(qMin(minPoint.z(), p.z()));
    maxPoint.setX(qMax(maxPoint.x(), p.x()));
    maxPoint.setY(qMax(maxPoint.y(), p.y()));
    maxPoint.setZ(qMax(maxPoint.z(), p.z()));
  }

  frameBoundingBox(minPoint, maxPoint, padding);
}

void ViewportCameraController::frameBoundingBox(const QVector3D &min,
                                                const QVector3D &max,
                                                float padding) {
  // Set target to center of bounding box
  m_target = (min + max) * 0.5f;

  // Calculate distance to fit bounding box
  QVector3D size = max - min;
  float maxDim = qMax(qMax(size.x(), size.y()), size.z());

  // Distance based on FOV (assuming 45 degrees)
  float fovRad = qDegreesToRadians(45.0f / 2.0f);
  m_distance = (maxDim * padding) / (2.0f * qTan(fovRad));

  clampValues();
  emit cameraChanged();
}

void ViewportCameraController::reset() {
  m_target = m_defaultTarget;
  m_distance = m_defaultDistance;
  m_yaw = m_defaultYaw;
  m_pitch = m_defaultPitch;
  m_velocityYaw = 0.0f;
  m_velocityPitch = 0.0f;
  m_velocityZoom = 0.0f;
  stopAnimation();
  emit cameraChanged();
}

void ViewportCameraController::clampValues() {
  m_distance = qBound(m_minDistance, m_distance, m_maxDistance);
  m_pitch = qBound(m_minPitch, m_pitch, m_maxPitch);
}

void ViewportCameraController::updateFromSpherical() {
  // Not needed - position() calculates on demand
}

void ViewportCameraController::startAnimation() {
  if (!m_animationTimer.isActive()) {
    m_animationTimer.start();
  }
}

void ViewportCameraController::stopAnimation() { m_animationTimer.stop(); }

void ViewportCameraController::onAnimationTick() {
  if (!m_enableDamping) {
    stopAnimation();
    return;
  }

  bool needsUpdate = false;

  // Apply yaw velocity
  if (qAbs(m_velocityYaw) > kVelocityThreshold) {
    m_yaw += m_velocityYaw;
    m_velocityYaw *= m_dampingFactor;
    // Wrap yaw
    while (m_yaw < 0)
      m_yaw += 360.0f;
    while (m_yaw >= 360)
      m_yaw -= 360.0f;
    needsUpdate = true;
  } else {
    m_velocityYaw = 0.0f;
  }

  // Apply pitch velocity
  if (qAbs(m_velocityPitch) > kVelocityThreshold) {
    m_pitch += m_velocityPitch;
    m_velocityPitch *= m_dampingFactor;
    needsUpdate = true;
  } else {
    m_velocityPitch = 0.0f;
  }

  // Apply zoom velocity
  if (qAbs(m_velocityZoom) > kVelocityThreshold) {
    float factor = qPow(1.1f, -m_velocityZoom);
    m_distance *= factor;
    m_velocityZoom *= m_dampingFactor;
    needsUpdate = true;
  } else {
    m_velocityZoom = 0.0f;
  }

  if (needsUpdate) {
    clampValues();
    emit cameraChanged();
  } else {
    // All velocities are zero, stop animation
    stopAnimation();
  }
}

} // namespace scene
} // namespace Raytracer
