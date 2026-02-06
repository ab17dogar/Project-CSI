#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../engine/render_runner.h"
#include "../engine/world.h"
#include "../util/vec3.h"
#include "gl_texture.h"

struct GLFWwindow;

class GuiApplication {
public:
  GuiApplication();
  ~GuiApplication();

  bool Init(int width, int height, const char *title);
  void Run();

private:
  void RenderUI();
  void HandleInput(float deltaTime);
  void RenderSingleSample();
  void ResetAccumulation();

  // Raytracer interaction
  void StartRender();
  void StopRender();
  void OnTileFinished(const std::vector<color> &bitmap, int w, int h,
                      const render::TileProgressStats &stats);

  // Window State
  GLFWwindow *m_Window{nullptr};

  // Raytracer State
  std::shared_ptr<world> m_World;
  std::vector<color> m_Bitmap;
  std::vector<color> m_AccumulationBuffer;
  int m_SampleCount{0};
  std::thread m_RenderThread;
  std::atomic<bool> m_IsRendering{false};
  std::atomic<bool> m_CancelFlag{false};
  bool m_InteractiveMode{true};

  // UI/Texture State
  GLTexture m_Texture;
  render::TileProgressStats m_Stats;
  bool m_TextureUpdatePending{false};
  std::mutex m_TextureMutex;

  // Camera State
  vec3 m_CameraPos{0, 1, 5};
  vec3 m_CameraRotation{0, 0, 0}; // Pitch, Yaw, Roll (degrees)
  float m_CameraFOV{60.0f};
  float m_MoveSpeed{5.0f};
  float m_MouseSensitivity{0.2f};

  // Input State
  bool m_ViewportHovered{false};
  bool m_ViewportFocused{false};
  bool m_MouseCaptured{false};
  double m_LastMouseX{0}, m_LastMouseY{0};
  bool m_FirstMouse{true};

  // Sky Settings
  vec3 m_SkyColorTop{0.5f, 0.7f, 1.0f};
  vec3 m_SkyColorBottom{1.0f, 1.0f, 1.0f};
  vec3 m_GroundColor{0.5f, 0.5f, 0.5f};

  // Render Config
  std::string m_ScenePath{"assets/cornell_water_scene.xml"};
  int m_RenderWidth{800};
  int m_RenderHeight{600};
  int m_RenderSamples{100};
  int m_MaxBounces{10};
  int m_RaysPerPixel{1}; // For interactive mode

  // Timing
  float m_LastFrameTime{0};

  // Progressive rendering state
  int m_CurrentRow{0};
};
