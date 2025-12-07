# Ray Tracer Desktop Application - Development Guide

## Overview
Transform your command-line ray tracer into a professional desktop application with a modern UI, similar to Unity/Unreal Engine interfaces.

---

## 1. GUI Framework Options

### Option A: Qt (Recommended for Professional UI)
**Pros:**
- Professional, polished UI
- Cross-platform (Windows, macOS, Linux)
- Rich widget library
- Excellent documentation
- Industry standard (used by Autodesk, Adobe, etc.)
- Built-in OpenGL support for viewport

**Cons:**
- Larger dependency (~50MB)
- Requires Qt license for commercial use (LGPL available)
- Steeper learning curve

**Installation:**
```bash
# macOS
brew install qt@6

# Ubuntu/Debian
sudo apt-get install qt6-base-dev qt6-tools-dev

# Or download from: https://www.qt.io/download
```

### Option B: Dear ImGui (Recommended for Quick Prototype)
**Pros:**
- Immediate mode GUI (very fast to develop)
- Lightweight
- Great for tools/editors
- Built-in OpenGL integration
- No licensing issues (MIT)

**Cons:**
- Less polished look
- More manual styling needed
- Better for tools than end-user apps

**Installation:**
```bash
# Add to your project
git submodule add https://github.com/ocornut/imgui.git src/3rdParty/imgui
```

### Option C: wxWidgets
**Pros:**
- Native look on each platform
- No licensing restrictions
- Mature and stable

**Cons:**
- Less modern appearance
- Smaller community than Qt

---

## 2. Recommended Architecture

### 2.1 Application Structure
```
RayTracerApp/
├── src/
│   ├── ui/                    # UI components
│   │   ├── MainWindow.h/cpp
│   │   ├── ViewportWidget.h/cpp
│   │   ├── SceneHierarchy.h/cpp
│   │   ├── PropertiesPanel.h/cpp
│   │   ├── MaterialEditor.h/cpp
│   │   └── RenderSettings.h/cpp
│   ├── engine/               # Your existing ray tracer
│   ├── core/                 # Core application logic
│   │   ├── Application.h/cpp
│   │   ├── SceneManager.h/cpp
│   │   └── RenderManager.h/cpp
│   └── main.cpp
└── CMakeLists.txt
```

### 2.2 Key Components

**MainWindow:** Main application window with menu bar, toolbars, dockable panels

**ViewportWidget:** Real-time preview of the scene (OpenGL widget showing progressive render)

**SceneHierarchy:** Tree view of all objects in the scene (spheres, meshes, lights)

**PropertiesPanel:** Edit selected object properties (position, rotation, scale, material)

**MaterialEditor:** Create and edit materials with live preview

**RenderSettings:** Configure render parameters (resolution, samples, depth, etc.)

---

## 3. Implementation Plan (Qt-based)

### Phase 1: Basic Window Setup (Week 1)

#### Step 1: Update CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.16)
project(RayTracerApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL)

# Your existing sources
set(SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/ViewportWidget.cpp
    # ... existing engine files
)

set(HEADERS
    src/ui/MainWindow.h
    src/ui/ViewportWidget.h
    # ... existing headers
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link Qt
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    # ... existing libraries
)

# Enable MOC (Meta-Object Compiler)
set(CMAKE_AUTOMOC ON)
```

#### Step 2: Create MainWindow
```cpp
// src/ui/MainWindow.h
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include "ViewportWidget.h"
#include "SceneHierarchy.h"
#include "PropertiesPanel.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openScene();
    void saveScene();
    void renderImage();
    void exportImage();

private:
    void setupUI();
    void setupMenus();
    void setupToolbars();
    void setupDockWidgets();

    ViewportWidget* viewport;
    SceneHierarchy* sceneHierarchy;
    PropertiesPanel* propertiesPanel;
    
    QDockWidget* hierarchyDock;
    QDockWidget* propertiesDock;
};

#endif
```

```cpp
// src/ui/MainWindow.cpp
#include "MainWindow.h"
#include <QFileDialog>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setupUI();
    setupMenus();
    setupToolbars();
    setupDockWidgets();
    
    setWindowTitle("Ray Tracer Studio");
    resize(1400, 900);
}

void MainWindow::setupUI() {
    // Create central viewport
    viewport = new ViewportWidget(this);
    setCentralWidget(viewport);
}

void MainWindow::setupMenus() {
    // File Menu
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Open Scene...", this, &MainWindow::openScene, QKeySequence::Open);
    fileMenu->addAction("&Save Scene...", this, &MainWindow::saveScene, QKeySequence::Save);
    fileMenu->addSeparator();
    fileMenu->addAction("&Export Image...", this, &MainWindow::exportImage);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close, QKeySequence::Quit);

    // Render Menu
    QMenu* renderMenu = menuBar()->addMenu("&Render");
    renderMenu->addAction("&Start Render", this, &MainWindow::renderImage, QKeySequence("F5"));
    renderMenu->addAction("&Stop Render", viewport, &ViewportWidget::stopRender);
}

void MainWindow::setupDockWidgets() {
    // Scene Hierarchy Dock
    sceneHierarchy = new SceneHierarchy(this);
    hierarchyDock = new QDockWidget("Scene Hierarchy", this);
    hierarchyDock->setWidget(sceneHierarchy);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);

    // Properties Dock
    propertiesPanel = new PropertiesPanel(this);
    propertiesDock = new QDockWidget("Properties", this);
    propertiesDock->setWidget(propertiesPanel);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);
}
```

### Phase 2: Viewport Widget (Week 2)

```cpp
// src/ui/ViewportWidget.h
#ifndef VIEWPORTWIDGET_H
#define VIEWPORTWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>
#include <QImage>
#include <memory>
#include "../engine/world.h"

class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget();

    void loadScene(const QString& filePath);
    void startRender();
    void stopRender();
    void setPreviewMode(bool enabled);

signals:
    void renderProgress(int percent);
    void renderComplete(const QImage& image);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void updatePreview();

private:
    void renderTile(int x, int y, int width, int height);
    QImage bitmapToQImage(const std::vector<color>& bitmap, int width, int height, int samples);

    std::shared_ptr<world> scene;
    QImage renderImage;
    QTimer* previewTimer;
    bool isRendering;
    bool previewMode;
    int previewSamples;
    
    // Camera controls
    float cameraYaw, cameraPitch;
    QPoint lastMousePos;
};

#endif
```

```cpp
// src/ui/ViewportWidget.cpp
#include "ViewportWidget.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <thread>
#include "../engine/factories/factory_methods.h"

ViewportWidget::ViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent), isRendering(false), previewMode(true), previewSamples(1),
      cameraYaw(0), cameraPitch(0) {
    previewTimer = new QTimer(this);
    connect(previewTimer, &QTimer::timeout, this, &ViewportWidget::updatePreview);
}

void ViewportWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
}

void ViewportWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (!renderImage.isNull()) {
        QPainter painter(this);
        painter.drawImage(rect(), renderImage);
    }
}

void ViewportWidget::loadScene(const QString& filePath) {
    scene = LoadScene(filePath.toStdString());
    if (scene) {
        updatePreview();
    }
}

void ViewportWidget::startRender() {
    if (!scene || isRendering) return;
    
    isRendering = true;
    
    // Run rendering in background thread
    std::thread renderThread([this]() {
        int width = scene->GetImageWidth();
        int height = scene->GetImageHeight();
        int samples = scene->GetSamplesPerPixel();
        std::vector<color> bitmap(width * height);
        
        // Use your existing TestBitmap function
        // TestBitmap(bitmap, threads, tile_size, false);
        
        renderImage = bitmapToQImage(bitmap, width, height, samples);
        isRendering = false;
        
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        emit renderComplete(renderImage);
    });
    
    renderThread.detach();
}

QImage ViewportWidget::bitmapToQImage(const std::vector<color>& bitmap, int width, int height, int samples) {
    QImage image(width, height, QImage::Format_RGB32);
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            color c = bitmap[(height - 1 - y) * width + x];
            // Apply gamma correction
            double scale = 1.0 / samples;
            double r = sqrt(scale * c.x());
            double g = sqrt(scale * c.y());
            double b = sqrt(scale * c.z());
            
            int ir = static_cast<int>(256 * clamp(r, 0.0, 0.999));
            int ig = static_cast<int>(256 * clamp(g, 0.0, 0.999));
            int ib = static_cast<int>(256 * clamp(b, 0.0, 0.999));
            
            image.setPixel(x, y, qRgb(ir, ig, ib));
        }
    }
    
    return image;
}
```

### Phase 3: Scene Hierarchy (Week 3)

```cpp
// src/ui/SceneHierarchy.h
#ifndef SCENEHIERARCHY_H
#define SCENEHIERARCHY_H

#include <QTreeWidget>
#include <memory>
#include "../engine/world.h"

class SceneHierarchy : public QTreeWidget {
    Q_OBJECT

public:
    SceneHierarchy(QWidget* parent = nullptr);
    void setScene(std::shared_ptr<world> scene);
    void addObject(const QString& name, std::shared_ptr<hittable> obj);

signals:
    void objectSelected(std::shared_ptr<hittable> obj);

private slots:
    void onItemSelectionChanged();

private:
    std::shared_ptr<world> currentScene;
};
```

### Phase 4: Properties Panel (Week 4)

```cpp
// src/ui/PropertiesPanel.h
#ifndef PROPERTIESPANEL_H
#define PROPERTIESPANEL_H

#include <QWidget>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <memory>
#include "../engine/hittable.h"

class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    PropertiesPanel(QWidget* parent = nullptr);
    void setObject(std::shared_ptr<hittable> obj);

private slots:
    void onPositionChanged();
    void onRotationChanged();
    void onScaleChanged();

private:
    void setupUI();
    void updateUI();

    std::shared_ptr<hittable> currentObject;
    
    QDoubleSpinBox* posX, *posY, *posZ;
    QDoubleSpinBox* rotX, *rotY, *rotZ;
    QDoubleSpinBox* scaleX, *scaleY, *scaleZ;
};
```

---

## 4. Key Features to Implement

### 4.1 Real-time Preview Mode
- Low sample count (1-4 samples per pixel)
- Progressive rendering (update as tiles complete)
- Interactive camera controls (mouse drag to rotate, scroll to zoom)

### 4.2 Scene Editor
- Drag-and-drop object creation
- Transform gizmos (move, rotate, scale)
- Object selection and deletion
- Undo/Redo system

### 4.3 Material Editor
- Visual material preview
- Color picker for albedo
- Sliders for roughness, metallic, etc.
- Material library (save/load materials)

### 4.4 Render Settings
- Resolution presets (720p, 1080p, 4K)
- Samples per pixel slider
- Max depth control
- Thread count selection
- Output format selection

### 4.5 File Management
- Save/Load scene files (XML)
- Export rendered images (PNG, JPEG, EXR)
- Recent files menu
- Project templates

---

## 5. Integration with Existing Code

### 5.1 Refactor for UI Integration

**Create RenderManager:**
```cpp
// src/core/RenderManager.h
class RenderManager : public QObject {
    Q_OBJECT

public:
    RenderManager(QObject* parent = nullptr);
    
    void setScene(std::shared_ptr<world> scene);
    void startRender(int width, int height, int samples, int maxDepth);
    void stopRender();
    
signals:
    void tileComplete(int x, int y, int width, int height, const std::vector<color>& tile);
    void renderComplete(const std::vector<color>& image);
    void progressUpdated(int percent);

private:
    std::shared_ptr<world> currentScene;
    std::atomic<bool> shouldStop;
    std::thread renderThread;
};
```

### 5.2 Make Rendering Thread-Safe
- Use signals/slots for thread communication
- Protect shared data with mutexes
- Use atomic flags for cancellation

---

## 6. Quick Start with ImGui (Alternative)

If you want to prototype faster:

```cpp
// src/main.cpp with ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

int main() {
    // Initialize GLFW and OpenGL
    // Initialize ImGui
    
    while (!glfwWindowShouldClose(window)) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // Viewport window
        ImGui::Begin("Viewport");
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        // Render your scene to texture
        ImGui::Image((void*)textureID, viewportSize);
        ImGui::End();
        
        // Properties panel
        ImGui::Begin("Properties");
        if (selectedObject) {
            float pos[3] = {x, y, z};
            ImGui::DragFloat3("Position", pos);
        }
        ImGui::End();
        
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}
```

---

## 7. Recommended Development Order

1. **Week 1-2:** Basic Qt window with viewport (OpenGL widget)
2. **Week 3:** Integrate existing renderer, show rendered image
3. **Week 4:** Add scene hierarchy panel
4. **Week 5:** Add properties panel for object editing
5. **Week 6:** Material editor
6. **Week 7:** Real-time preview mode
7. **Week 8:** Polish UI, add menus, toolbars, shortcuts

---

## 8. Resources

- **Qt Documentation:** https://doc.qt.io/qt-6/
- **Qt Examples:** https://doc.qt.io/qt-6/qtexamples.html
- **ImGui Documentation:** https://github.com/ocornut/imgui
- **OpenGL Tutorial:** https://learnopengl.com/

---

## 9. Next Steps

1. Choose your GUI framework (Qt recommended for professional look)
2. Set up the project structure
3. Create basic MainWindow
4. Integrate your existing renderer
5. Add UI components incrementally

Would you like me to help you set up the initial Qt project structure or create specific UI components?

