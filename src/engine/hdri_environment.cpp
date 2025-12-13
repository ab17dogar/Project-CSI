#include "hdri_environment.h"
#include "../3rdParty/stb_image.h"

bool hdri_environment::load(const std::string &filename) {
  int components;

  // Load image via stb_image
  unsigned char *img_data =
      stbi_load(filename.c_str(), &width, &height, &components, 3);

  if (!img_data) {
    std::cerr << "Failed to load environment: " << filename << std::endl;
    return false;
  }

  // Convert to linear float data (gamma decode)
  data.resize(width * height * 3);
  for (int i = 0; i < width * height * 3; i++) {
    // Approximate sRGB to linear conversion
    data[i] = static_cast<float>(pow(img_data[i] / 255.0, 2.2));
  }

  stbi_image_free(img_data);

  std::cerr << "Loaded environment: " << filename << " (" << width << "x"
            << height << ")" << std::endl;

  return true;
}
