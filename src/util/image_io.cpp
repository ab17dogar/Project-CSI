#include "image_io.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

#include "../3rdParty/stb_image_write.h"

namespace {

bool is_little_endian_host() {
  const uint32_t probe = 1;
  unsigned char first;
  std::memcpy(&first, &probe, 1);
  return first == 1;
}

// Portable 32-bit byte swap of a float in place.
void swap_bytes(float &f) {
  unsigned char b[4];
  std::memcpy(b, &f, 4);
  std::swap(b[0], b[3]);
  std::swap(b[1], b[2]);
  std::memcpy(&f, b, 4);
}

} // namespace

double linear_to_srgb(double c) {
  if (!(c > 0.0))
    return 0.0;
  return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

double srgb_to_linear(double c) {
  if (!(c > 0.0))
    return 0.0;
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

bool write_pfm(const std::string &path, const image_rgb &img) {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    return false;
  // Negative scale: little-endian data. Rows are stored bottom to top.
  out << "PF\n" << img.width << ' ' << img.height << "\n-1.0\n";
  const size_t row = static_cast<size_t>(img.width) * 3;
  for (int y = img.height - 1; y >= 0; --y) {
    const float *src = img.data.data() + static_cast<size_t>(y) * row;
    if (!is_little_endian_host()) {
      std::vector<float> swapped(src, src + row);
      for (float &f : swapped)
        swap_bytes(f);
      out.write(reinterpret_cast<const char *>(swapped.data()),
                static_cast<std::streamsize>(row * sizeof(float)));
    } else {
      out.write(reinterpret_cast<const char *>(src),
                static_cast<std::streamsize>(row * sizeof(float)));
    }
  }
  return static_cast<bool>(out);
}

bool read_pfm(const std::string &path, image_rgb &out, std::string *error) {
  auto fail = [&](const std::string &msg) {
    if (error)
      *error = path + ": " + msg;
    return false;
  };
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return fail("cannot open");

  std::string magic;
  int w = 0, h = 0;
  double scale = 0.0;
  in >> magic >> w >> h >> scale;
  if (!in || (magic != "PF" && magic != "Pf") || w <= 0 || h <= 0 ||
      scale == 0.0)
    return fail("not a valid PFM header");
  in.get(); // the single whitespace byte that ends the header

  const int channels = magic == "PF" ? 3 : 1;
  const bool file_little = scale < 0.0;
  const size_t row = static_cast<size_t>(w) * channels;
  std::vector<float> buf(row);
  out = image_rgb(w, h);
  for (int y = h - 1; y >= 0; --y) {
    in.read(reinterpret_cast<char *>(buf.data()),
            static_cast<std::streamsize>(row * sizeof(float)));
    if (!in)
      return fail("truncated pixel data");
    if (file_little != is_little_endian_host()) {
      for (float &f : buf)
        swap_bytes(f);
    }
    for (int x = 0; x < w; ++x) {
      const size_t dst = (static_cast<size_t>(y) * w + x) * 3;
      for (int c = 0; c < 3; ++c)
        out.data[dst + c] = buf[static_cast<size_t>(x) * channels +
                                (channels == 3 ? c : 0)];
    }
  }
  return true;
}

static bool write_png_bytes(const std::string &path, int w, int h,
                            const std::vector<unsigned char> &bytes) {
  return stbi_write_png(path.c_str(), w, h, 3, bytes.data(), w * 3) != 0;
}

bool write_png_srgb(const std::string &path, const image_rgb &img,
                    double exposure) {
  std::vector<unsigned char> bytes(img.data.size());
  for (size_t i = 0; i < img.data.size(); ++i) {
    const double v = linear_to_srgb(img.data[i] * exposure);
    bytes[i] = static_cast<unsigned char>(
        std::clamp(static_cast<int>(256.0 * v), 0, 255));
  }
  return write_png_bytes(path, img.width, img.height, bytes);
}

bool write_png_display(const std::string &path, const image_rgb &img) {
  std::vector<unsigned char> bytes(img.data.size());
  for (size_t i = 0; i < img.data.size(); ++i)
    bytes[i] = static_cast<unsigned char>(
        std::clamp(static_cast<int>(255.0 * img.data[i] + 0.5), 0, 255));
  return write_png_bytes(path, img.width, img.height, bytes);
}
