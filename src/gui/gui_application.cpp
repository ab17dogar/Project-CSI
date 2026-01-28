#include "gui_application.h"

#include <cmath>
#include <iostream>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

#define GL_SILENCE_DEPRECATION
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>

#include "../engine/camera.h"
#include "../engine/config.h"
#include "../engine/factories/factory_methods.h"

// Helper for degree to radian conversion
constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

GuiApplication::GuiApplication() {}

GuiApplication::~GuiApplication() {
  StopRender();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (m_Window) {
    glfwDestroyWindow(m_Window);
    glfwTerminate();
  }
}

bool GuiApplication::Init(int width, int height, const char *title) {
  if (!glfwInit())
    return false;

  // GL 3.2 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (!m_Window)
    return false;

  glfwMakeContextCurrent(m_Window);
  glfwSwapInterval(1); // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Init default Texture (Black 1x1)
  m_Texture.Init(1, 1);
  m_LastFrameTime = (float)glfwGetTime();

  return true;
}

void GuiApplication::ResetAccumulation() {
  m_SampleCount = 0;
  m_CurrentRow = 0; // Reset progressive rendering
  if (!m_AccumulationBuffer.empty()) {
    std::fill(m_AccumulationBuffer.begin(), m_AccumulationBuffer.end(),
              color(0, 0, 0));
  }
  // Keep old m_Bitmap visible - new render will overwrite it progressively
}

void GuiApplication::RenderSingleSample() {
  if (!m_World || !m_World->pcamera)
    return;

  const int width = m_RenderWidth;
  const int height = m_RenderHeight;
  const size_t pixelCount =
      static_cast<size_t>(width) * static_cast<size_t>(height);

  // Safety check: ensure buffers are properly sized
  if (m_AccumulationBuffer.size() != pixelCount ||
      m_Bitmap.size() != pixelCount) {
    return; // Buffers not initialized, skip this frame
  }

  // Update camera from GUI state
  vec3 lookDir(std::sin(m_CameraRotation.y() * DEG_TO_RAD) *
                   std::cos(m_CameraRotation.x() * DEG_TO_RAD),
               -std::sin(m_CameraRotation.x() * DEG_TO_RAD),
               -std::cos(m_CameraRotation.y() * DEG_TO_RAD) *
                   std::cos(m_CameraRotation.x() * DEG_TO_RAD));
  vec3 lookAt = m_CameraPos + lookDir;

  // Recreate camera with current GUI state
  m_World->pcamera =
      std::make_shared<camera>(m_CameraPos, lookAt, vec3(0, 1, 0), m_CameraFOV,
                               (double)width / (double)height);

  // Apply sky colors from GUI to world
  m_World->skyColorTop =
      color(m_SkyColorTop.x(), m_SkyColorTop.y(), m_SkyColorTop.z());
  m_World->skyColorBottom =
      color(m_SkyColorBottom.x(), m_SkyColorBottom.y(), m_SkyColorBottom.z());
  m_World->groundColor =
      color(m_GroundColor.x(), m_GroundColor.y(), m_GroundColor.z());

  // Scattered pixel rendering - render pixels spread across entire image
  // This gives instant (blurry) preview that refines over time
  const int pixelsPerFrame =
      std::max(1000, (int)pixelCount / 20); // ~5% per frame

  static std::mt19937 rng(42);

  for (int i = 0; i < pixelsPerFrame; ++i) {
    // Pick a random pixel
    int x = rng() % width;
    int y = rng() % height;

    color sample = render::RenderPixel(*m_World, x, y, m_SampleCount);
    size_t idx = (height - 1 - y) * width + x;

    // Blend new sample with existing (exponential moving average for smooth
    // updates)
    double alpha = 1.0 / (m_SampleCount + 1);
    m_Bitmap[idx] = m_Bitmap[idx] * (1.0 - alpha) + sample * alpha;
    m_AccumulationBuffer[idx] = m_AccumulationBuffer[idx] + sample;
  }

  // Track progress
  m_CurrentRow += pixelsPerFrame;
  if (m_CurrentRow >= (int)pixelCount) {
    m_CurrentRow = 0;
    m_SampleCount++;
  }

  // Update texture
  m_Texture.Update(m_Bitmap, width, height);
}

void GuiApplication::HandleInput(float deltaTime) {
  // Camera Movement - Arrow keys always work, WASD when not typing
  vec3 forward(std::sin(m_CameraRotation.y() * DEG_TO_RAD), 0,
               -std::cos(m_CameraRotation.y() * DEG_TO_RAD));
  vec3 right = cross(forward, vec3(0, 1, 0));
  forward = unit_vector(forward);
  right = unit_vector(right);

  float speed = m_MoveSpeed * deltaTime;
  bool moved = false;

  // Arrow keys (always work)
  if (glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_PRESS) {
    m_CameraPos = m_CameraPos + forward * speed;
    moved = true;
  }
  if (glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_PRESS) {
    m_CameraPos = m_CameraPos - forward * speed;
    moved = true;
  }
  if (glfwGetKey(m_Window, GLFW_KEY_LEFT) == GLFW_PRESS) {
    m_CameraPos = m_CameraPos - right * speed;
    moved = true;
  }
  if (glfwGetKey(m_Window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
    m_CameraPos = m_CameraPos + right * speed;
    moved = true;
  }
  if (glfwGetKey(m_Window, GLFW_KEY_PAGE_UP) == GLFW_PRESS) {
    m_CameraPos = m_CameraPos + vec3(0, 1, 0) * speed;
    moved = true;
  }
  if (glfwGetKey(m_Window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) {
    m_CameraPos = m_CameraPos - vec3(0, 1, 0) * speed;
    moved = true;
  }

  // WASD (when not typing in text field)
  ImGuiIO &io = ImGui::GetIO();
  if (!io.WantTextInput) {
    if (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS) {
      m_CameraPos = m_CameraPos + forward * speed;
      moved = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS) {
      m_CameraPos = m_CameraPos - forward * speed;
      moved = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS) {
      m_CameraPos = m_CameraPos - right * speed;
      moved = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS) {
      m_CameraPos = m_CameraPos + right * speed;
      moved = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_PRESS) {
      m_CameraPos = m_CameraPos + vec3(0, 1, 0) * speed;
      moved = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
      m_CameraPos = m_CameraPos - vec3(0, 1, 0) * speed;
      moved = true;
    }
  }

  if (moved) {
    ResetAccumulation();
  }

  // Mouse Look (Right-click drag)
  if (m_ViewportHovered &&
      glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
    double mouseX, mouseY;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);

    if (m_FirstMouse) {
      m_LastMouseX = mouseX;
      m_LastMouseY = mouseY;
      m_FirstMouse = false;
    }

    float xOffset = (float)(mouseX - m_LastMouseX) * m_MouseSensitivity;
    float yOffset =
        (float)(m_LastMouseY - mouseY) * m_MouseSensitivity; // Inverted

    m_LastMouseX = mouseX;
    m_LastMouseY = mouseY;

    if (std::abs(xOffset) > 0.01f || std::abs(yOffset) > 0.01f) {
      m_CameraRotation =
          vec3(m_CameraRotation.x() + yOffset, m_CameraRotation.y() + xOffset,
               m_CameraRotation.z());

      // Clamp pitch
      float pitch = m_CameraRotation.x();
      if (pitch > 89.0f)
        pitch = 89.0f;
      if (pitch < -89.0f)
        pitch = -89.0f;
      m_CameraRotation =
          vec3(pitch, m_CameraRotation.y(), m_CameraRotation.z());

      ResetAccumulation();
    }
  } else {
    m_FirstMouse = true;
  }
}

void GuiApplication::Run() {
  while (!glfwWindowShouldClose(m_Window)) {
    glfwPollEvents();

    float currentTime = (float)glfwGetTime();
    float deltaTime = currentTime - m_LastFrameTime;
    m_LastFrameTime = currentTime;

    // Handle camera input
    HandleInput(deltaTime);

    // Progressive rendering (if scene loaded and not batch rendering)
    if (m_World && !m_IsRendering && m_InteractiveMode) {
      RenderSingleSample();
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    RenderUI();

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(m_Window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_Window);

    // Texture update from render thread (for batch mode)
    {
      std::lock_guard<std::mutex> lock(m_TextureMutex);
      if (m_TextureUpdatePending) {
        int w = m_RenderWidth;
        int h = m_RenderHeight;
        if (m_Bitmap.size() >= w * h) {
          m_Texture.Update(m_Bitmap, w, h);
        }
        m_TextureUpdatePending = false;
      }
    }
  }
}

void GuiApplication::RenderUI() {
  // DockSpace
  ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

  // 1. Settings Panel
  ImGui::Begin("Settings");

  // --- Transform Section ---
  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool changed = false;
    float pos[3] = {(float)m_CameraPos.x(), (float)m_CameraPos.y(),
                    (float)m_CameraPos.z()};
    if (ImGui::DragFloat3("Position", pos, 0.1f)) {
      m_CameraPos = vec3(pos[0], pos[1], pos[2]);
      changed = true;
    }
    float rot[3] = {(float)m_CameraRotation.x(), (float)m_CameraRotation.y(),
                    (float)m_CameraRotation.z()};
    if (ImGui::DragFloat3("Rotation", rot, 1.0f)) {
      m_CameraRotation = vec3(rot[0], rot[1], rot[2]);
      changed = true;
    }
    if (ImGui::DragFloat("FOV", &m_CameraFOV, 1.0f, 10.0f, 120.0f)) {
      changed = true;
    }
    if (changed)
      ResetAccumulation();
  }

  // --- Render Settings Section ---
  if (ImGui::CollapsingHeader("Render Settings",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    bool changed = false;
    if (ImGui::InputInt("Width", &m_RenderWidth))
      changed = true;
    if (ImGui::InputInt("Height", &m_RenderHeight))
      changed = true;
    if (ImGui::InputInt("Samples", &m_RenderSamples))
      changed = true;
    if (ImGui::InputInt("Max Bounces", &m_MaxBounces))
      changed = true;
    if (changed)
      ResetAccumulation();
  }

  // --- Controls Section ---
  if (ImGui::CollapsingHeader("Controls")) {
    ImGui::DragFloat("Move Speed", &m_MoveSpeed, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Sensitivity", &m_MouseSensitivity, 0.01f, 0.01f, 1.0f);
    ImGui::Separator();
    ImGui::Text("W/A/S/D - Move");
    ImGui::Text("Space/Shift - Up/Down");
    ImGui::Text("Right Click + Drag - Look");
  }

  // --- Sky Settings Section ---
  if (ImGui::CollapsingHeader("Sky Settings")) {
    bool changed = false;
    float skyTop[3] = {(float)m_SkyColorTop.x(), (float)m_SkyColorTop.y(),
                       (float)m_SkyColorTop.z()};
    if (ImGui::ColorEdit3("Sky Top", skyTop)) {
      m_SkyColorTop = vec3(skyTop[0], skyTop[1], skyTop[2]);
      changed = true;
    }
    float skyBottom[3] = {(float)m_SkyColorBottom.x(),
                          (float)m_SkyColorBottom.y(),
                          (float)m_SkyColorBottom.z()};
    if (ImGui::ColorEdit3("Sky Bottom", skyBottom)) {
      m_SkyColorBottom = vec3(skyBottom[0], skyBottom[1], skyBottom[2]);
      changed = true;
    }
    float ground[3] = {(float)m_GroundColor.x(), (float)m_GroundColor.y(),
                       (float)m_GroundColor.z()};
    if (ImGui::ColorEdit3("Ground", ground)) {
      m_GroundColor = vec3(ground[0], ground[1], ground[2]);
      changed = true;
    }
    if (changed)
      ResetAccumulation();
  }

  ImGui::Separator();

  // --- Scene and Render Buttons ---
  char sceneBuf[256];
  strncpy(sceneBuf, m_ScenePath.c_str(), sizeof(sceneBuf));
  if (ImGui::InputText("Scene Path", sceneBuf, sizeof(sceneBuf))) {
    m_ScenePath = std::string(sceneBuf);
  }

  if (m_IsRendering) {
    if (ImGui::Button("Stop Render", ImVec2(120, 30))) {
      StopRender();
    }
    ImGui::SameLine();
    ImGui::Text("Progress: %zu / %zu tiles", m_Stats.tilesDone,
                m_Stats.totalTiles);
    ImGui::ProgressBar(
        (float)m_Stats.tilesDone /
        (float)(m_Stats.totalTiles > 0 ? m_Stats.totalTiles : 1));
  } else {
    // Load Scene for interactive preview
    if (ImGui::Button("Load Scene", ImVec2(120, 30))) {
      m_World = LoadScene(m_ScenePath);
      if (m_World) {
        m_World->pconfig->IMAGE_WIDTH = m_RenderWidth;
        m_World->pconfig->IMAGE_HEIGHT = m_RenderHeight;
        m_World->pconfig->SAMPLES_PER_PIXEL = 1; // Interactive uses 1 spp
        m_World->pconfig->MAX_DEPTH = m_MaxBounces;

        // Get camera position from scene
        if (m_World->pcamera) {
          m_CameraPos = m_World->pcamera->LOOK_FROM;
          // Calculate rotation from look direction
          vec3 lookDir =
              m_World->pcamera->LOOK_AT - m_World->pcamera->LOOK_FROM;
          lookDir = unit_vector(lookDir);
          m_CameraRotation =
              vec3(std::asin(-lookDir.y()) / DEG_TO_RAD,
                   std::atan2(lookDir.x(), -lookDir.z()) / DEG_TO_RAD, 0);
          m_CameraFOV = (float)m_World->pcamera->FOV;
        }

        // Initialize buffers
        size_t pixelCount = m_RenderWidth * m_RenderHeight;
        m_AccumulationBuffer.resize(pixelCount);
        m_Bitmap.resize(pixelCount);
        std::fill(m_AccumulationBuffer.begin(), m_AccumulationBuffer.end(),
                  color(0, 0, 0));
        std::fill(m_Bitmap.begin(), m_Bitmap.end(), color(0, 0, 0));
        m_SampleCount = 0;

        // Initialize texture
        m_Texture.Init(m_RenderWidth, m_RenderHeight);

        std::cout << "Scene loaded for interactive preview." << std::endl;
      } else {
        std::cerr << "Failed to load scene: " << m_ScenePath << std::endl;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Batch Render", ImVec2(120, 30))) {
      StartRender();
    }
  }

  ImGui::Text("Accumulated Samples: %d", m_SampleCount);

  ImGui::End();

  // 2. Viewport
  ImGui::Begin("Viewport");

  m_ViewportHovered = ImGui::IsWindowHovered();
  m_ViewportFocused = ImGui::IsWindowFocused();

  ImVec2 vMin = ImGui::GetWindowContentRegionMin();
  ImVec2 vMax = ImGui::GetWindowContentRegionMax();
  float w = vMax.x - vMin.x;
  float h = vMax.y - vMin.y;

  int texW = m_Texture.GetWidth();
  int texH = m_Texture.GetHeight();
  float aspect = (float)texW / (float)texH;

  float displayH = w / aspect;
  if (displayH > h) {
    displayH = h;
    w = h * aspect;
  }

  ImGui::Image((void *)(intptr_t)m_Texture.GetRendererID(),
               ImVec2(w, displayH));
  ImGui::End();
}

void GuiApplication::StartRender() {
  if (m_IsRendering)
    return;

  // Load Scene
  m_World = LoadScene(m_ScenePath);
  if (!m_World) {
    std::cerr << "Failed to load scene: " << m_ScenePath << std::endl;
    return;
  }

  // Apply Overrides from GUI
  m_World->pconfig->IMAGE_WIDTH = m_RenderWidth;
  m_World->pconfig->IMAGE_HEIGHT = m_RenderHeight;
  m_World->pconfig->SAMPLES_PER_PIXEL = m_RenderSamples;
  m_World->pconfig->MAX_DEPTH = m_MaxBounces;

  // Apply camera from GUI
  // TODO: Update world camera with m_CameraPos and m_CameraRotation

  // Reset State
  m_Bitmap.resize(m_World->GetImageWidth() * m_World->GetImageHeight());
  std::fill(m_Bitmap.begin(), m_Bitmap.end(), color(0, 0, 0));
  m_Stats = render::TileProgressStats();

  // Start Thread
  m_IsRendering = true;
  m_CancelFlag = false;
  m_InteractiveMode = false; // Disable interactive rendering during batch

  m_RenderThread = std::thread([this]() {
    render::RenderSceneToBitmap(
        *m_World, m_Bitmap, std::thread::hardware_concurrency(),
        32,    // Tile Size
        false, // Tile Debug
        [this](const std::vector<color> &bmp, int w, int h,
               const render::TileProgressStats &stats) {
          this->OnTileFinished(bmp, w, h, stats);
        },
        &m_CancelFlag);
    m_IsRendering = false;
  });
}

void GuiApplication::StopRender() {
  if (m_IsRendering) {
    m_CancelFlag = true;
    if (m_RenderThread.joinable()) {
      m_RenderThread.join();
    }
    m_IsRendering = false;
  }
}

void GuiApplication::OnTileFinished(const std::vector<color> &bmp, int w, int h,
                                    const render::TileProgressStats &stats) {
  std::lock_guard<std::mutex> lock(m_TextureMutex);
  m_Bitmap = bmp;
  m_Stats = stats;
  m_TextureUpdatePending = true;
}
