#include "gl_texture.h"

// Include OpenGL headers appropriate for the platform
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <iostream>

GLTexture::~GLTexture() {
  if (m_RendererID) {
    glDeleteTextures(1, &m_RendererID);
  }
}

void GLTexture::Init(int width, int height) {
  m_Width = width;
  m_Height = height;

  glGenTextures(1, &m_RendererID);
  glBindTexture(GL_TEXTURE_2D, m_RendererID);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Prepare buffer
  m_Buffer.resize(width * height * 4); // RGBA
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, m_Buffer.data());
}

void GLTexture::Update(const std::vector<color> &bitmap, int width,
                       int height) {
  if (width != m_Width || height != m_Height) {
    Init(width, height);
  }

  // Convert Raytracer Color (linear float) to RGBA8 (sRGB)
  // Similar to SaveImage logic but for real-time display
  size_t pixelCount = width * height;
  if (m_Buffer.size() != pixelCount * 4) {
    m_Buffer.resize(pixelCount * 4);
  }

  for (int i = 0; i < pixelCount; ++i) {
    color c = bitmap[i];

    // Simple clamp and gamma correction (approximate sqrt)
    auto r = sqrt(c.x());
    auto g = sqrt(c.y());
    auto b = sqrt(c.z());

    unsigned char rc =
        static_cast<unsigned char>(255.99 * (r < 0 ? 0 : (r > 1 ? 1 : r)));
    unsigned char gc =
        static_cast<unsigned char>(255.99 * (g < 0 ? 0 : (g > 1 ? 1 : g)));
    unsigned char bc =
        static_cast<unsigned char>(255.99 * (b < 0 ? 0 : (b > 1 ? 1 : b)));

    m_Buffer[i * 4 + 0] = rc;
    m_Buffer[i * 4 + 1] = gc;
    m_Buffer[i * 4 + 2] = bc;
    m_Buffer[i * 4 + 3] = 255; // Alpha
  }

  glBindTexture(GL_TEXTURE_2D, m_RendererID);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                  GL_UNSIGNED_BYTE, m_Buffer.data());
}
