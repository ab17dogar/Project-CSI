#pragma once

#include "../util/vec3.h"
#include <vector>

// Simple wrapper to manage an OpenGL texture
class GLTexture {
public:
  GLTexture() = default;
  ~GLTexture();

  void Init(int width, int height);
  void Update(const std::vector<color> &bitmap, int width, int height);
  unsigned int GetRendererID() const { return m_RendererID; }
  int GetWidth() const { return m_Width; }
  int GetHeight() const { return m_Height; }

private:
  unsigned int m_RendererID{0};
  int m_Width{0};
  int m_Height{0};
  std::vector<unsigned char> m_Buffer;
};
