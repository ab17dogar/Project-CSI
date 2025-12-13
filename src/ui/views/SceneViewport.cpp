#include "SceneViewport.h"
#include "../controllers/SelectionManager.h"
#include "../controllers/ViewportCameraController.h"
#include "../models/MaterialDefinition.h"
#include "../models/SceneDocument.h"
#include "../models/SceneNode.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>

namespace Raytracer {
namespace scene {

// ===== Shader Sources =====

static const char *basicVertexShader = R"(
    #version 330 core
    layout(location = 0) in vec3 position;
    layout(location = 1) in vec3 normal;
    
    uniform mat4 mvp;
    uniform mat4 model;
    uniform mat3 normalMatrix;
    
    out vec3 fragNormal;
    out vec3 fragPos;
    
    void main() {
        gl_Position = mvp * vec4(position, 1.0);
        fragPos = vec3(model * vec4(position, 1.0));
        fragNormal = normalMatrix * normal;
    }
)";

static const char *basicFragmentShader = R"(
    #version 330 core
    in vec3 fragNormal;
    in vec3 fragPos;
    
    uniform vec3 objectColor;
    uniform vec3 lightDir;
    uniform vec3 viewPos;
    uniform bool useShading;
    uniform bool isSelected;
    uniform bool isWireframe;
    
    out vec4 fragColor;
    
    void main() {
        vec3 color = objectColor;
        
        if (isWireframe) {
            fragColor = vec4(color, 1.0);
            return;
        }
        
        if (useShading) {
            vec3 norm = normalize(fragNormal);
            vec3 light = normalize(lightDir);
            
            // Ambient
            float ambient = 0.3;
            
            // Diffuse
            float diff = max(dot(norm, light), 0.0);
            
            // Simple rim light for selected objects
            if (isSelected) {
                vec3 viewDir = normalize(viewPos - fragPos);
                float rim = 1.0 - max(dot(viewDir, norm), 0.0);
                rim = pow(rim, 3.0) * 0.5;
                color = mix(color, vec3(1.0, 0.6, 0.0), rim);
            }
            
            vec3 result = (ambient + diff * 0.7) * color;
            fragColor = vec4(result, 1.0);
        } else {
            fragColor = vec4(color, 1.0);
        }
    }
)";

static const char *gridVertexShader = R"(
    #version 330 core
    layout(location = 0) in vec3 position;
    layout(location = 1) in vec3 color;
    
    uniform mat4 mvp;
    
    out vec3 fragColor;
    
    void main() {
        gl_Position = mvp * vec4(position, 1.0);
        fragColor = color;
    }
)";

static const char *gridFragmentShader = R"(
    #version 330 core
    in vec3 fragColor;
    out vec4 outColor;
    
    void main() {
        outColor = vec4(fragColor, 1.0);
    }
)";

// ===== Constructor / Destructor =====

SceneViewport::SceneViewport(QWidget *parent)
    : QOpenGLWidget(parent),
      m_cameraController(std::make_unique<ViewportCameraController>()) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  // Important: Prevent Qt from trying to use native OpenGL context sharing
  // which can cause rendering issues on macOS
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_DontCreateNativeAncestors);

  // Configure surface format for OpenGL 3.3 Core
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  format.setSamples(4); // MSAA
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  setFormat(format);

  m_frameTimer.start();
}

SceneViewport::~SceneViewport() {
  // Only clean up if OpenGL was initialized
  if (!isValid()) {
    return;
  }

  makeCurrent();

  // Clean up OpenGL resources - check isCreated() before destroying
  if (m_sphereVBO.isCreated())
    m_sphereVBO.destroy();
  if (m_sphereIBO.isCreated())
    m_sphereIBO.destroy();
  if (m_sphereVAO.isCreated())
    m_sphereVAO.destroy();

  if (m_cubeVBO.isCreated())
    m_cubeVBO.destroy();
  if (m_cubeIBO.isCreated())
    m_cubeIBO.destroy();
  if (m_cubeVAO.isCreated())
    m_cubeVAO.destroy();

  if (m_gridVBO.isCreated())
    m_gridVBO.destroy();
  if (m_gridVAO.isCreated())
    m_gridVAO.destroy();

  if (m_axisVBO.isCreated())
    m_axisVBO.destroy();
  if (m_axisVAO.isCreated())
    m_axisVAO.destroy();

  if (m_triangleVBO.isCreated())
    m_triangleVBO.destroy();
  if (m_triangleVAO.isCreated())
    m_triangleVAO.destroy();

  doneCurrent();
}

// ===== Scene Binding =====

void SceneViewport::setSceneDocument(SceneDocument *document) {
  if (m_document) {
    disconnect(m_document, nullptr, this, nullptr);
  }

  m_document = document;

  if (m_document) {
    connect(m_document, &SceneDocument::documentChanged, this,
            &SceneViewport::onSceneChanged);
    connect(m_document, &SceneDocument::nodeAdded, this,
            [this](SceneNode *) { update(); });
    connect(m_document, &SceneDocument::nodeRemoved, this,
            [this](const QUuid &) { update(); });
    connect(m_document, &SceneDocument::nodeChanged, this,
            [this](SceneNode *) { update(); });
  }

  update();
}

void SceneViewport::setSelectionManager(SelectionManager *manager) {
  if (m_selectionManager) {
    disconnect(m_selectionManager, nullptr, this, nullptr);
  }

  m_selectionManager = manager;

  if (m_selectionManager) {
    connect(m_selectionManager, &SelectionManager::selectionChanged, this,
            &SceneViewport::onSelectionChanged);
  }

  update();
}

// ===== Camera Control =====

void SceneViewport::resetCamera() {
  if (m_cameraController) {
    m_cameraController->reset();
    update();
  }
}

void SceneViewport::focusOnSelection() {
  // TODO: Calculate bounding box of selection and focus camera
  update();
}

void SceneViewport::focusOnAll() {
  // TODO: Calculate bounding box of all objects and focus camera
  resetCamera();
}

// ===== Rendering Options =====

void SceneViewport::setRenderMode(RenderMode mode) {
  m_renderMode = mode;
  update();
}

void SceneViewport::setShadingMode(ShadingMode mode) {
  m_shadingMode = mode;
  update();
}

void SceneViewport::setShowGrid(bool show) {
  m_showGrid = show;
  update();
}

void SceneViewport::setShowAxes(bool show) {
  m_showAxes = show;
  update();
}

void SceneViewport::setBackgroundColor(const QColor &color) {
  m_backgroundColor = color;
  update();
}

// ===== Ray Picking =====

QVector3D SceneViewport::screenToWorld(const QPoint &screenPos,
                                       float depth) const {
  QMatrix4x4 mvp = m_projectionMatrix * m_viewMatrix;
  QMatrix4x4 invMvp = mvp.inverted();

  float x = (2.0f * screenPos.x()) / width() - 1.0f;
  float y = 1.0f - (2.0f * screenPos.y()) / height();

  QVector4D clipPos(x, y, depth * 2.0f - 1.0f, 1.0f);
  QVector4D worldPos = invMvp * clipPos;

  if (qFuzzyIsNull(worldPos.w()))
    return QVector3D();

  return worldPos.toVector3D() / worldPos.w();
}

SceneNode *SceneViewport::pickObject(const QPoint &screenPos) {
  // TODO: Implement ray-object intersection
  // For now, return nullptr
  Q_UNUSED(screenPos);
  return nullptr;
}

// ===== Slots =====

void SceneViewport::requestUpdate() { update(); }

void SceneViewport::onSceneChanged() { update(); }

void SceneViewport::onSelectionChanged() { update(); }

// ===== OpenGL Lifecycle =====

void SceneViewport::initializeGL() {
  initializeOpenGLFunctions();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);
  glEnable(GL_LINE_SMOOTH);

  initShaders();
  initGeometry();

  // Set initial camera position
  m_cameraController->setTarget(QVector3D(0, 0, 0));
  m_cameraController->setDistance(10.0f);
  m_cameraController->setPitch(-30.0f);
  m_cameraController->setYaw(45.0f);
}

void SceneViewport::resizeGL(int w, int h) {
  glViewport(0, 0, w, h);

  // Update projection matrix
  float aspect = static_cast<float>(w) / static_cast<float>(h);
  m_projectionMatrix.setToIdentity();
  m_projectionMatrix.perspective(45.0f, aspect, 0.1f, 1000.0f);
}

void SceneViewport::paintGL() {
  // Clear
  glClearColor(m_backgroundColor.redF(), m_backgroundColor.greenF(),
               m_backgroundColor.blueF(), 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Update view matrix from camera
  updateMatrices();

  // Render grid and axes
  if (m_showGrid)
    renderGrid();
  if (m_showAxes)
    renderAxes();

  // Render scene
  renderScene();

  // FPS calculation
  m_frameCount++;
  if (m_frameTimer.elapsed() >= 1000) {
    m_currentFps = m_frameCount * 1000.0f / m_frameTimer.elapsed();
    m_frameCount = 0;
    m_frameTimer.restart();
    emit fpsUpdated(m_currentFps);
  }

  emit viewportUpdated();
}

// ===== Input Events =====

void SceneViewport::mousePressEvent(QMouseEvent *event) {
  m_lastMousePos = event->pos();
  m_isDragging = true;

  if (event->button() == Qt::LeftButton &&
      !(event->modifiers() & (Qt::AltModifier | Qt::ControlModifier))) {
    // Object picking
    SceneNode *picked = pickObject(event->pos());
    emit objectPicked(picked, event->modifiers());
  }

  event->accept();
}

void SceneViewport::mouseReleaseEvent(QMouseEvent *event) {
  m_isDragging = false;
  event->accept();
}

void SceneViewport::mouseMoveEvent(QMouseEvent *event) {
  QPoint delta = event->pos() - m_lastMousePos;
  m_lastMousePos = event->pos();

  if (m_isDragging && m_cameraController) {
    bool leftButton = event->buttons() & Qt::LeftButton;
    bool middleButton = event->buttons() & Qt::MiddleButton;
    bool rightButton = event->buttons() & Qt::RightButton;
    bool shiftKey = event->modifiers() & Qt::ShiftModifier;

    if (rightButton || (leftButton && shiftKey)) {
      // Pan (right drag or shift+left drag)
      m_cameraController->pan(delta.x() * 0.01f, delta.y() * 0.01f);
      update();
    } else if (leftButton || middleButton) {
      // Orbit (left drag or middle drag) - primary rotation control
      m_cameraController->orbit(delta.x() * 0.5f, delta.y() * 0.5f);
      update();
    }
  }

  event->accept();
}

void SceneViewport::wheelEvent(QWheelEvent *event) {
  if (m_cameraController) {
    float delta = event->angleDelta().y() / 120.0f;
    m_cameraController->zoom(delta);
    update();
  }
  event->accept();
}

void SceneViewport::keyPressEvent(QKeyEvent *event) {
  switch (event->key()) {
  case Qt::Key_F:
    if (event->modifiers() & Qt::ShiftModifier) {
      focusOnAll();
    } else {
      focusOnSelection();
    }
    break;
  case Qt::Key_Home:
    resetCamera();
    break;
  case Qt::Key_G:
    setShowGrid(!m_showGrid);
    break;
  default:
    QOpenGLWidget::keyPressEvent(event);
    return;
  }
  event->accept();
}

void SceneViewport::keyReleaseEvent(QKeyEvent *event) {
  QOpenGLWidget::keyReleaseEvent(event);
}

// ===== Initialization =====

void SceneViewport::initShaders() {
  // Basic shader for objects
  m_basicShader = std::make_unique<QOpenGLShaderProgram>();
  if (!m_basicShader->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                              basicVertexShader)) {
    qWarning() << "Basic vertex shader compilation failed:"
               << m_basicShader->log();
  }
  if (!m_basicShader->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                              basicFragmentShader)) {
    qWarning() << "Basic fragment shader compilation failed:"
               << m_basicShader->log();
  }
  if (!m_basicShader->link()) {
    qWarning() << "Basic shader linking failed:" << m_basicShader->log();
  }

  // Grid shader
  m_gridShader = std::make_unique<QOpenGLShaderProgram>();
  if (!m_gridShader->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                             gridVertexShader)) {
    qWarning() << "Grid vertex shader compilation failed:"
               << m_gridShader->log();
  }
  if (!m_gridShader->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                             gridFragmentShader)) {
    qWarning() << "Grid fragment shader compilation failed:"
               << m_gridShader->log();
  }
  if (!m_gridShader->link()) {
    qWarning() << "Grid shader linking failed:" << m_gridShader->log();
  }
}

void SceneViewport::initGeometry() {
  initGridGeometry();
  initAxisGeometry();
  initPrimitiveGeometry();
}

void SceneViewport::initGridGeometry() {
  std::vector<float> vertices;
  const int gridSize = 10;
  const float gridStep = 1.0f;
  const float gridColor = 0.4f;
  const float majorColor = 0.5f;

  // Grid lines along X
  for (int i = -gridSize; i <= gridSize; ++i) {
    float z = i * gridStep;
    float color = (i == 0) ? majorColor : gridColor;

    vertices.push_back(-gridSize * gridStep);
    vertices.push_back(0.0f);
    vertices.push_back(z);
    vertices.push_back(color);
    vertices.push_back(color);
    vertices.push_back(color);

    vertices.push_back(gridSize * gridStep);
    vertices.push_back(0.0f);
    vertices.push_back(z);
    vertices.push_back(color);
    vertices.push_back(color);
    vertices.push_back(color);
  }

  // Grid lines along Z
  for (int i = -gridSize; i <= gridSize; ++i) {
    float x = i * gridStep;
    float color = (i == 0) ? majorColor : gridColor;

    vertices.push_back(x);
    vertices.push_back(0.0f);
    vertices.push_back(-gridSize * gridStep);
    vertices.push_back(color);
    vertices.push_back(color);
    vertices.push_back(color);

    vertices.push_back(x);
    vertices.push_back(0.0f);
    vertices.push_back(gridSize * gridStep);
    vertices.push_back(color);
    vertices.push_back(color);
    vertices.push_back(color);
  }

  m_gridVertexCount = static_cast<int>(vertices.size() / 6);

  m_gridVAO.create();
  m_gridVAO.bind();

  m_gridVBO.create();
  m_gridVBO.bind();
  m_gridVBO.allocate(vertices.data(),
                     static_cast<int>(vertices.size() * sizeof(float)));

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));

  m_gridVAO.release();
}

void SceneViewport::initAxisGeometry() {
  std::vector<float> vertices = {
      // X axis (red)
      0.0f,
      0.0f,
      0.0f,
      1.0f,
      0.2f,
      0.2f,
      5.0f,
      0.0f,
      0.0f,
      1.0f,
      0.2f,
      0.2f,
      // Y axis (green)
      0.0f,
      0.0f,
      0.0f,
      0.2f,
      1.0f,
      0.2f,
      0.0f,
      5.0f,
      0.0f,
      0.2f,
      1.0f,
      0.2f,
      // Z axis (blue)
      0.0f,
      0.0f,
      0.0f,
      0.2f,
      0.2f,
      1.0f,
      0.0f,
      0.0f,
      5.0f,
      0.2f,
      0.2f,
      1.0f,
  };

  m_axisVertexCount = 6;

  m_axisVAO.create();
  m_axisVAO.bind();

  m_axisVBO.create();
  m_axisVBO.bind();
  m_axisVBO.allocate(vertices.data(),
                     static_cast<int>(vertices.size() * sizeof(float)));

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));

  m_axisVAO.release();
}

void SceneViewport::initPrimitiveGeometry() {
  // === Sphere geometry ===
  std::vector<float> sphereVertices;
  std::vector<unsigned int> sphereIndices;

  const int latSegments = 16;
  const int lonSegments = 32;

  for (int lat = 0; lat <= latSegments; ++lat) {
    float theta = lat * M_PI / latSegments;
    float sinTheta = std::sin(theta);
    float cosTheta = std::cos(theta);

    for (int lon = 0; lon <= lonSegments; ++lon) {
      float phi = lon * 2.0f * M_PI / lonSegments;
      float sinPhi = std::sin(phi);
      float cosPhi = std::cos(phi);

      float x = cosPhi * sinTheta;
      float y = cosTheta;
      float z = sinPhi * sinTheta;

      // Position
      sphereVertices.push_back(x);
      sphereVertices.push_back(y);
      sphereVertices.push_back(z);
      // Normal (same as position for unit sphere)
      sphereVertices.push_back(x);
      sphereVertices.push_back(y);
      sphereVertices.push_back(z);
    }
  }

  for (int lat = 0; lat < latSegments; ++lat) {
    for (int lon = 0; lon < lonSegments; ++lon) {
      int first = lat * (lonSegments + 1) + lon;
      int second = first + lonSegments + 1;

      sphereIndices.push_back(first);
      sphereIndices.push_back(second);
      sphereIndices.push_back(first + 1);

      sphereIndices.push_back(second);
      sphereIndices.push_back(second + 1);
      sphereIndices.push_back(first + 1);
    }
  }

  m_sphereIndexCount = static_cast<int>(sphereIndices.size());

  m_sphereVAO.create();
  m_sphereVAO.bind();

  m_sphereVBO.create();
  m_sphereVBO.bind();
  m_sphereVBO.allocate(sphereVertices.data(),
                       static_cast<int>(sphereVertices.size() * sizeof(float)));

  m_sphereIBO.create();
  m_sphereIBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
  m_sphereIBO.bind();
  m_sphereIBO.allocate(
      sphereIndices.data(),
      static_cast<int>(sphereIndices.size() * sizeof(unsigned int)));

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));

  m_sphereVAO.release();

  // === Cube geometry ===
  std::vector<float> cubeVertices = {
      // Front face
      -0.5f,
      -0.5f,
      0.5f,
      0.0f,
      0.0f,
      1.0f,
      0.5f,
      -0.5f,
      0.5f,
      0.0f,
      0.0f,
      1.0f,
      0.5f,
      0.5f,
      0.5f,
      0.0f,
      0.0f,
      1.0f,
      -0.5f,
      0.5f,
      0.5f,
      0.0f,
      0.0f,
      1.0f,
      // Back face
      -0.5f,
      -0.5f,
      -0.5f,
      0.0f,
      0.0f,
      -1.0f,
      -0.5f,
      0.5f,
      -0.5f,
      0.0f,
      0.0f,
      -1.0f,
      0.5f,
      0.5f,
      -0.5f,
      0.0f,
      0.0f,
      -1.0f,
      0.5f,
      -0.5f,
      -0.5f,
      0.0f,
      0.0f,
      -1.0f,
      // Top face
      -0.5f,
      0.5f,
      -0.5f,
      0.0f,
      1.0f,
      0.0f,
      -0.5f,
      0.5f,
      0.5f,
      0.0f,
      1.0f,
      0.0f,
      0.5f,
      0.5f,
      0.5f,
      0.0f,
      1.0f,
      0.0f,
      0.5f,
      0.5f,
      -0.5f,
      0.0f,
      1.0f,
      0.0f,
      // Bottom face
      -0.5f,
      -0.5f,
      -0.5f,
      0.0f,
      -1.0f,
      0.0f,
      0.5f,
      -0.5f,
      -0.5f,
      0.0f,
      -1.0f,
      0.0f,
      0.5f,
      -0.5f,
      0.5f,
      0.0f,
      -1.0f,
      0.0f,
      -0.5f,
      -0.5f,
      0.5f,
      0.0f,
      -1.0f,
      0.0f,
      // Right face
      0.5f,
      -0.5f,
      -0.5f,
      1.0f,
      0.0f,
      0.0f,
      0.5f,
      0.5f,
      -0.5f,
      1.0f,
      0.0f,
      0.0f,
      0.5f,
      0.5f,
      0.5f,
      1.0f,
      0.0f,
      0.0f,
      0.5f,
      -0.5f,
      0.5f,
      1.0f,
      0.0f,
      0.0f,
      // Left face
      -0.5f,
      -0.5f,
      -0.5f,
      -1.0f,
      0.0f,
      0.0f,
      -0.5f,
      -0.5f,
      0.5f,
      -1.0f,
      0.0f,
      0.0f,
      -0.5f,
      0.5f,
      0.5f,
      -1.0f,
      0.0f,
      0.0f,
      -0.5f,
      0.5f,
      -0.5f,
      -1.0f,
      0.0f,
      0.0f,
  };

  std::vector<unsigned int> cubeIndices = {
      0,  1,  2,  0,  2,  3,  // Front
      4,  5,  6,  4,  6,  7,  // Back
      8,  9,  10, 8,  10, 11, // Top
      12, 13, 14, 12, 14, 15, // Bottom
      16, 17, 18, 16, 18, 19, // Right
      20, 21, 22, 20, 22, 23  // Left
  };

  m_cubeIndexCount = static_cast<int>(cubeIndices.size());

  m_cubeVAO.create();
  m_cubeVAO.bind();

  m_cubeVBO.create();
  m_cubeVBO.bind();
  m_cubeVBO.allocate(cubeVertices.data(),
                     static_cast<int>(cubeVertices.size() * sizeof(float)));

  m_cubeIBO.create();
  m_cubeIBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
  m_cubeIBO.bind();
  m_cubeIBO.allocate(
      cubeIndices.data(),
      static_cast<int>(cubeIndices.size() * sizeof(unsigned int)));

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));

  m_cubeVAO.release();

  // === Triangle VAO (dynamic) ===
  m_triangleVAO.create();
  m_triangleVAO.bind();

  m_triangleVBO.create();
  m_triangleVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
  m_triangleVBO.bind();
  m_triangleVBO.allocate(18 * sizeof(float)); // 3 vertices * 6 floats

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));

  m_triangleVAO.release();
}

// ===== Rendering =====

void SceneViewport::renderScene() {
  if (!m_document)
    return;

  m_basicShader->bind();

  // Set light direction (sun direction from scene or default)
  QVector3D lightDir(0.4f, 1.0f, 0.3f);
  if (m_document) {
    const auto &sunSettings = m_document->sun();
    lightDir = QVector3D(sunSettings.direction.x, sunSettings.direction.y,
                         sunSettings.direction.z);
  }
  lightDir.normalize();
  m_basicShader->setUniformValue("lightDir", lightDir);
  m_basicShader->setUniformValue("viewPos", m_cameraController->position());
  m_basicShader->setUniformValue("useShading",
                                 m_shadingMode == ShadingMode::Smooth);

  // Render from root
  SceneNode *root = m_document->rootNode();
  if (root) {
    QMatrix4x4 identity;
    for (SceneNode *child : root->children()) {
      renderNode(child, identity);
    }
  }

  m_basicShader->release();
}

void SceneViewport::renderNode(const SceneNode *node,
                               const QMatrix4x4 &parentMatrix) {
  if (!node || !node->isVisible())
    return;

  QMatrix4x4 modelMatrix = parentMatrix * nodeToMatrix(node);
  QVector3D color = getMaterialColor(node);
  bool selected = m_selectionManager && m_selectionManager->isSelected(node);

  switch (node->geometryType()) {
  case GeometryType::Sphere:
    renderSphere(modelMatrix, color, selected);
    break;
  case GeometryType::Triangle:
    renderTriangle(node, modelMatrix, color, selected);
    break;
  case GeometryType::Mesh:
    renderMesh(node, modelMatrix, color, selected);
    break;
  case GeometryType::Cube:
    // Render cube similar to sphere
    break;
  default:
    break;
  }

  // Render children
  for (SceneNode *child : node->children()) {
    renderNode(child, modelMatrix);
  }
}

void SceneViewport::renderSphere(const QMatrix4x4 &modelMatrix,
                                 const QVector3D &color, bool selected) {
  QMatrix4x4 mvp = m_projectionMatrix * m_viewMatrix * modelMatrix;
  QMatrix3x3 normalMatrix = modelMatrix.normalMatrix();

  m_basicShader->setUniformValue("mvp", mvp);
  m_basicShader->setUniformValue("model", modelMatrix);
  m_basicShader->setUniformValue("normalMatrix", normalMatrix);
  m_basicShader->setUniformValue("objectColor", color);
  m_basicShader->setUniformValue("isSelected", selected);
  m_basicShader->setUniformValue("isWireframe", false);

  m_sphereVAO.bind();
  m_sphereIBO.bind(); // Explicitly bind IBO

  if (m_renderMode == RenderMode::Solid ||
      m_renderMode == RenderMode::SolidWithWireframe) {
    glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, nullptr);
  }

  if (m_renderMode == RenderMode::Wireframe ||
      m_renderMode == RenderMode::SolidWithWireframe) {
    m_basicShader->setUniformValue("isWireframe", true);
    m_basicShader->setUniformValue("objectColor", QVector3D(0.0f, 0.0f, 0.0f));
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, nullptr);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  m_sphereIBO.release();
  m_sphereVAO.release();
}

void SceneViewport::renderTriangle(const SceneNode *node,
                                   const QMatrix4x4 &modelMatrix,
                                   const QVector3D &color, bool selected) {
  const auto &params = node->geometryParams();

  // Calculate normal
  QVector3D v0(params.v0.x, params.v0.y, params.v0.z);
  QVector3D v1(params.v1.x, params.v1.y, params.v1.z);
  QVector3D v2(params.v2.x, params.v2.y, params.v2.z);
  QVector3D normal = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();

  // Update triangle VBO
  float vertices[] = {params.v0.x, params.v0.y, params.v0.z, normal.x(),
                      normal.y(),  normal.z(),  params.v1.x, params.v1.y,
                      params.v1.z, normal.x(),  normal.y(),  normal.z(),
                      params.v2.x, params.v2.y, params.v2.z, normal.x(),
                      normal.y(),  normal.z()};

  m_triangleVAO.bind();
  m_triangleVBO.bind();
  m_triangleVBO.write(0, vertices, sizeof(vertices));

  QMatrix4x4 mvp = m_projectionMatrix * m_viewMatrix * modelMatrix;
  QMatrix3x3 normalMatrix = modelMatrix.normalMatrix();

  m_basicShader->setUniformValue("mvp", mvp);
  m_basicShader->setUniformValue("model", modelMatrix);
  m_basicShader->setUniformValue("normalMatrix", normalMatrix);
  m_basicShader->setUniformValue("objectColor", color);
  m_basicShader->setUniformValue("isSelected", selected);
  m_basicShader->setUniformValue("isWireframe", false);

  glDrawArrays(GL_TRIANGLES, 0, 3);

  if (m_renderMode == RenderMode::Wireframe ||
      m_renderMode == RenderMode::SolidWithWireframe) {
    m_basicShader->setUniformValue("isWireframe", true);
    m_basicShader->setUniformValue("objectColor", QVector3D(0.0f, 0.0f, 0.0f));
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  m_triangleVAO.release();
}

void SceneViewport::renderMesh(const SceneNode *node,
                               const QMatrix4x4 &modelMatrix,
                               const QVector3D &color, bool selected) {
  // TODO: Implement mesh loading and rendering
  // For now, render a cube as placeholder
  Q_UNUSED(node);

  QMatrix4x4 mvp = m_projectionMatrix * m_viewMatrix * modelMatrix;
  QMatrix3x3 normalMatrix = modelMatrix.normalMatrix();

  m_basicShader->setUniformValue("mvp", mvp);
  m_basicShader->setUniformValue("model", modelMatrix);
  m_basicShader->setUniformValue("normalMatrix", normalMatrix);
  m_basicShader->setUniformValue("objectColor", color);
  m_basicShader->setUniformValue("isSelected", selected);
  m_basicShader->setUniformValue("isWireframe", false);

  m_cubeVAO.bind();

  if (m_renderMode == RenderMode::Solid ||
      m_renderMode == RenderMode::SolidWithWireframe) {
    glDrawElements(GL_TRIANGLES, m_cubeIndexCount, GL_UNSIGNED_INT, nullptr);
  }

  if (m_renderMode == RenderMode::Wireframe ||
      m_renderMode == RenderMode::SolidWithWireframe) {
    m_basicShader->setUniformValue("isWireframe", true);
    m_basicShader->setUniformValue("objectColor", QVector3D(0.0f, 0.0f, 0.0f));
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawElements(GL_TRIANGLES, m_cubeIndexCount, GL_UNSIGNED_INT, nullptr);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  m_cubeVAO.release();
}

void SceneViewport::renderGrid() {
  m_gridShader->bind();

  QMatrix4x4 mvp = m_projectionMatrix * m_viewMatrix;
  m_gridShader->setUniformValue("mvp", mvp);

  m_gridVAO.bind();
  glDrawArrays(GL_LINES, 0, m_gridVertexCount);
  m_gridVAO.release();

  m_gridShader->release();
}

void SceneViewport::renderAxes() {
  m_gridShader->bind();

  QMatrix4x4 mvp = m_projectionMatrix * m_viewMatrix;
  m_gridShader->setUniformValue("mvp", mvp);

  glLineWidth(2.0f);
  m_axisVAO.bind();
  glDrawArrays(GL_LINES, 0, m_axisVertexCount);
  m_axisVAO.release();
  glLineWidth(1.0f);

  m_gridShader->release();
}

void SceneViewport::renderSelectionOutline(const SceneNode *node,
                                           const QMatrix4x4 &modelMatrix) {
  // TODO: Implement selection outline rendering
  Q_UNUSED(node);
  Q_UNUSED(modelMatrix);
}

// ===== Utility =====

QMatrix4x4 SceneViewport::nodeToMatrix(const SceneNode *node) const {
  QMatrix4x4 matrix;
  if (!node || !node->transform())
    return matrix;

  const auto *transform = node->transform();
  const auto &pos = transform->position();
  const auto &rot = transform->rotation();
  const auto &scale = transform->scale();

  matrix.translate(pos.x, pos.y, pos.z);
  matrix.rotate(rot.x, 1.0f, 0.0f, 0.0f);
  matrix.rotate(rot.y, 0.0f, 1.0f, 0.0f);
  matrix.rotate(rot.z, 0.0f, 0.0f, 1.0f);
  matrix.scale(scale.x, scale.y, scale.z);

  // For spheres, also apply radius
  if (node->geometryType() == GeometryType::Sphere) {
    float radius = node->geometryParams().radius;
    matrix.scale(radius);
  }

  return matrix;
}

QVector3D SceneViewport::getMaterialColor(const SceneNode *node) const {
  if (!node || !m_document)
    return QVector3D(0.7f, 0.7f, 0.7f);

  if (auto *mat = m_document->findMaterial(node->materialId())) {
    const auto &color = mat->color();
    return QVector3D(color.r, color.g, color.b);
  }

  return QVector3D(0.7f, 0.7f, 0.7f); // Default gray
}

void SceneViewport::updateMatrices() {
  if (m_cameraController) {
    m_viewMatrix = m_cameraController->viewMatrix();
  } else {
    m_viewMatrix.setToIdentity();
    m_viewMatrix.lookAt(QVector3D(5, 5, 5), QVector3D(0, 0, 0),
                        QVector3D(0, 1, 0));
  }
}

} // namespace scene
} // namespace Raytracer
