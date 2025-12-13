#ifndef RAYTRACER_SCENE_VIEWPORT_H
#define RAYTRACER_SCENE_VIEWPORT_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QVector3D>
#include <QElapsedTimer>
#include <memory>

namespace Raytracer {
namespace scene {

class SceneDocument;
class SceneNode;
class ViewportCameraController;
class SelectionManager;

/**
 * @brief OpenGL viewport widget for 3D scene preview
 * 
 * Provides real-time 3D visualization of the scene with:
 * - Wireframe/solid rendering of scene objects
 * - Camera orbit, pan, and zoom controls
 * - Selection highlighting
 * - Transform gizmos (future)
 * - Grid and axes display
 */
class SceneViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
    
public:
    enum class RenderMode {
        Wireframe,
        Solid,
        SolidWithWireframe
    };
    
    enum class ShadingMode {
        Flat,
        Smooth
    };
    
    explicit SceneViewport(QWidget *parent = nullptr);
    ~SceneViewport() override;
    
    // Scene binding
    void setSceneDocument(SceneDocument *document);
    SceneDocument* sceneDocument() const { return m_document; }
    
    // Selection
    void setSelectionManager(SelectionManager *manager);
    SelectionManager* selectionManager() const { return m_selectionManager; }
    
    // Camera control
    ViewportCameraController* cameraController() const { return m_cameraController.get(); }
    void resetCamera();
    void focusOnSelection();
    void focusOnAll();
    
    // Rendering options
    void setRenderMode(RenderMode mode);
    RenderMode renderMode() const { return m_renderMode; }
    
    void setShadingMode(ShadingMode mode);
    ShadingMode shadingMode() const { return m_shadingMode; }
    
    void setShowGrid(bool show);
    bool showGrid() const { return m_showGrid; }
    
    void setShowAxes(bool show);
    bool showAxes() const { return m_showAxes; }
    
    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_backgroundColor; }
    
    // Ray picking
    QVector3D screenToWorld(const QPoint &screenPos, float depth = 0.0f) const;
    SceneNode* pickObject(const QPoint &screenPos);
    
signals:
    void objectPicked(SceneNode *node, Qt::KeyboardModifiers modifiers);
    void viewportUpdated();
    void fpsUpdated(float fps);
    
public slots:
    void requestUpdate();
    void onSceneChanged();
    void onSelectionChanged();
    
protected:
    // OpenGL lifecycle
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    
    // Input events
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    
private:
    // Initialization
    void initShaders();
    void initGeometry();
    void initGridGeometry();
    void initAxisGeometry();
    void initPrimitiveGeometry();
    
    // Rendering
    void renderScene();
    void renderNode(const SceneNode *node, const QMatrix4x4 &parentMatrix);
    void renderSphere(const QMatrix4x4 &modelMatrix, const QVector3D &color, bool selected);
    void renderTriangle(const SceneNode *node, const QMatrix4x4 &modelMatrix, 
                       const QVector3D &color, bool selected);
    void renderMesh(const SceneNode *node, const QMatrix4x4 &modelMatrix,
                   const QVector3D &color, bool selected);
    void renderGrid();
    void renderAxes();
    void renderSelectionOutline(const SceneNode *node, const QMatrix4x4 &modelMatrix);
    
    // Utility
    QMatrix4x4 nodeToMatrix(const SceneNode *node) const;
    QVector3D getMaterialColor(const SceneNode *node) const;
    void updateMatrices();
    
private:
    // Scene data
    SceneDocument *m_document = nullptr;
    SelectionManager *m_selectionManager = nullptr;
    std::unique_ptr<ViewportCameraController> m_cameraController;
    
    // OpenGL resources
    std::unique_ptr<QOpenGLShaderProgram> m_basicShader;
    std::unique_ptr<QOpenGLShaderProgram> m_gridShader;
    
    // Primitive geometry VAOs/VBOs
    QOpenGLVertexArrayObject m_sphereVAO;
    QOpenGLBuffer m_sphereVBO{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_sphereIBO{QOpenGLBuffer::IndexBuffer};
    int m_sphereIndexCount = 0;
    
    QOpenGLVertexArrayObject m_cubeVAO;
    QOpenGLBuffer m_cubeVBO{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_cubeIBO{QOpenGLBuffer::IndexBuffer};
    int m_cubeIndexCount = 0;
    
    QOpenGLVertexArrayObject m_gridVAO;
    QOpenGLBuffer m_gridVBO;
    int m_gridVertexCount = 0;
    
    QOpenGLVertexArrayObject m_axisVAO;
    QOpenGLBuffer m_axisVBO;
    int m_axisVertexCount = 0;
    
    QOpenGLVertexArrayObject m_triangleVAO;
    QOpenGLBuffer m_triangleVBO;
    
    // View matrices
    QMatrix4x4 m_projectionMatrix;
    QMatrix4x4 m_viewMatrix;
    
    // Rendering settings
    RenderMode m_renderMode = RenderMode::SolidWithWireframe;
    ShadingMode m_shadingMode = ShadingMode::Flat;
    bool m_showGrid = true;
    bool m_showAxes = true;
    QColor m_backgroundColor = QColor(51, 51, 51); // Dark gray
    
    // Selection colors
    QVector3D m_selectionColor = QVector3D(1.0f, 0.6f, 0.0f); // Orange
    QVector3D m_hoverColor = QVector3D(0.8f, 0.8f, 0.2f);     // Yellow
    
    // Performance tracking
    QElapsedTimer m_frameTimer;
    int m_frameCount = 0;
    float m_currentFps = 0.0f;
    
    // Mouse state
    QPoint m_lastMousePos;
    bool m_isDragging = false;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_SCENE_VIEWPORT_H
