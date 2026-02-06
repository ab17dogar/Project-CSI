#include "defs.h"
#include "engine/camera.h"
#include "engine/config.h"
#include "engine/factories/factory_methods.h"
#include "engine/mesh.h"
#include "engine/render_runner.h"
#include "engine/sun.h"
#include "engine/world.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

// stb image write (minimal wrapper header)
#include "3rdParty/stb_image_write.h"
#include "render_presets.h"
#include "util/logging.h"

using namespace std;

namespace {

std::filesystem::path LocateAssetsRoot() {
  namespace fs = std::filesystem;
  fs::path current = fs::current_path();
  for (int i = 0; i < 6; ++i) {
    fs::path candidate = current / "assets";
    std::error_code ec;
    if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
      return candidate;
    }
    if (!current.has_parent_path()) {
      break;
    }
    current = current.parent_path();
  }
  return {};
}

std::string ResolveScenePath(const std::string &input) {
  namespace fs = std::filesystem;
  std::vector<fs::path> candidates;
  const fs::path provided(input);

  // First check assets/ directory as the primary location
  const fs::path assetsRoot = LocateAssetsRoot();
  if (!assetsRoot.empty()) {
    candidates.push_back(assetsRoot / provided);
  }

  if (provided.is_absolute()) {
    candidates.push_back(provided);
  } else {
    candidates.push_back(fs::current_path() / provided);
    if (auto parent = fs::current_path().parent_path(); !parent.empty()) {
      candidates.push_back(parent / provided);
    }
  }

  for (const auto &candidate : candidates) {
    if (candidate.empty()) {
      continue;
    }
    std::error_code ec;
    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
      return candidate.string();
    }
  }
  return {};
}

} // namespace

// Hittable List aka Scene:
hittable_list scene;

shared_ptr<world> pworld;

void SaveImage(world &sceneWorld, const string &fileName,
               const vector<color> &bitmap);
// int testObjLoader();

// Logging flags defined in util/logging.cpp

int main(int argc, char **argv) {
  // Default CLI-controlled values
  string scenePath = "objects.xml";
  string outPathFlag;
  unsigned int threads = std::thread::hardware_concurrency();
  int tile_size = 64;
  bool tile_debug = false;
  int widthOverride = -1;
  int samplesOverride = -1;
  bool useBVH = false;
  bool useDenoiser = true;
  const Raytracer::presets::RenderPresetDefinition *presetDefinition = nullptr;

  // Simple argv parser
  for (int i = 1; i < argc; ++i) {
    string a = argv[i];
    if (a == "--scene" && i + 1 < argc) {
      scenePath = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      outPathFlag = argv[++i];
    } else if (a == "--threads" && i + 1 < argc) {
      threads = (unsigned int)atoi(argv[++i]);
    } else if (a == "--tile-size" && i + 1 < argc) {
      tile_size = atoi(argv[++i]);
    } else if (a == "--tile-debug") {
      tile_debug = true;
    } else if (a == "--width" && i + 1 < argc) {
      widthOverride = atoi(argv[++i]);
    } else if (a == "--samples" && i + 1 < argc) {
      samplesOverride = atoi(argv[++i]);
    } else if (a == "--bvh") {
      useBVH = true;
    } else if (a == "--linear") {
      useBVH = false;
    } else if (a == "--no-denoise") {
      useDenoiser = false;
    } else if (a == "--denoise") {
      useDenoiser = true;
    } else if (a == "--preset" && i + 1 < argc) {
      const std::string presetName = argv[++i];
      presetDefinition = Raytracer::presets::findPreset(presetName);
      if (!presetDefinition) {
        cerr << "Unknown preset '" << presetName << "'. Valid presets:";
        for (const auto &definition : Raytracer::presets::kRenderPresets) {
          cerr << ' ' << definition.name;
        }
        cerr << endl;
        return 4;
      }
    } else if (a == "-h" || a == "--help") {
      cout
          << "Usage: Raytracer [--scene <file>] [--out <file>] [--threads N] "
             "[--preset NAME]\n"
          << "                 [--width W] [--samples S] [--bvh|--linear] "
             "[--no-denoise]\n"
          << "Options:\n"
          << "  --scene <file>   Scene XML file (default: objects.xml)\n"
          << "  --out <file>     Output image path (default: build/image.png)\n"
          << "  --threads N      Number of render threads\n"
          << "  --preset NAME    Use preset (Preview, Draft, Final)\n"
          << "  --width W        Override image width\n"
          << "  --samples S      Override samples per pixel\n"
          << "  --bvh            Use BVH acceleration (faster for large "
             "scenes)\n"
          << "  --linear         Use linear traversal\n"
          << "  --denoise        Enable OIDN AI denoiser (default)\n"
          << "  --no-denoise     Disable denoiser\n"
          << "  --quiet          Suppress progress output\n"
          << "  --verbose        Extra debug output\n";
      return 0;
    } else if (a == "--quiet") {
      g_quiet = true;
    } else if (a == "--verbose") {
      g_verbose = true;
    }
  }

  // Load scene file from current working directory (or as provided)
  const std::string resolvedScenePath = ResolveScenePath(scenePath);
  if (resolvedScenePath.empty()) {
    cerr << "Could not find scene file: " << scenePath
         << "\nPlease provide --scene <path> or place objects.xml in the "
            "working directory."
         << endl;
    return 2;
  }

  std::ifstream sceneFile(resolvedScenePath);
  if (!sceneFile.good()) {
    cerr << "Could not open scene file: " << resolvedScenePath << endl;
    return 2;
  }
  sceneFile.close();

  pworld = LoadScene(resolvedScenePath);
  if (!pworld) {
    cerr << "Failed to load scene file: " << resolvedScenePath << endl;
    return 3;
  }

  // Apply preset if provided before per-value overrides
  if (presetDefinition) {
    pworld->pconfig->IMAGE_WIDTH = presetDefinition->width;
    pworld->pconfig->IMAGE_HEIGHT = presetDefinition->height;
    pworld->pconfig->SAMPLES_PER_PIXEL = presetDefinition->samples;
    pworld->pconfig->ASPECT_RATIO =
        static_cast<double>(presetDefinition->width) /
        static_cast<double>(presetDefinition->height);
  }

  // Apply CLI overrides if provided
  if (widthOverride > 0) {
    pworld->pconfig->IMAGE_WIDTH = widthOverride;
    pworld->pconfig->IMAGE_HEIGHT =
        static_cast<int>(widthOverride / pworld->pconfig->ASPECT_RATIO);
  }
  if (samplesOverride > 0) {
    pworld->pconfig->SAMPLES_PER_PIXEL = samplesOverride;
  }

  // Apply BVH acceleration if requested
  if (useBVH) {
    pworld->pconfig->acceleration = AccelerationMethod::BVH;
  }

  // Apply denoiser setting
  pworld->pconfig->enableDenoiser = useDenoiser;

  vector<color> bitmap;

  // Decide output location: either CLI override or default behavior
  string outPath;
  char cwdBuf[4096];
  if (!outPathFlag.empty()) {
    outPath = outPathFlag;
  } else if (getcwd(cwdBuf, sizeof(cwdBuf)) != nullptr) {
    string cwd(cwdBuf);
    // If running from the build directory itself, write to output/image.png
    if (!cwd.empty() && cwd.substr(cwd.find_last_of("/\\") + 1) == "build") {
      outPath = string("output/image.png");
    } else {
      outPath = string("build/output/image.png");
    }
  } else {
    outPath = string("build/output/image.png");
  }

  // Ensure output directory exists
  {
    namespace fs = std::filesystem;
    fs::path outPathFs(outPath);
    if (outPathFs.has_parent_path()) {
      std::error_code ec;
      fs::create_directories(outPathFs.parent_path(), ec);
    }
  }


  // Startup summary (stderr) so it doesn't mix with progress output
  if (!g_quiet.load()) {
    cerr << "Scene: " << resolvedScenePath << "\n";
    cerr << "Output: " << outPath << "\n";
    cerr << "Threads: " << threads << "\n";
    cerr << "Image size: " << pworld->pconfig->IMAGE_WIDTH << "x"
         << pworld->pconfig->IMAGE_HEIGHT << "\n";
    cerr << "Samples: " << pworld->pconfig->SAMPLES_PER_PIXEL << "\n";
    cerr << "Acceleration: " << (useBVH ? "BVH" : "Linear") << "\n";
    if (presetDefinition) {
      cerr << "Preset: " << presetDefinition->name << "\n";
    }
    // Mesh diagnostics
    if (!g_attempted_meshes.empty()) {
      cerr << "Attempted meshes:";
      for (auto &m : g_attempted_meshes)
        cerr << ' ' << m;
      cerr << '\n';
    }
    if (!g_loaded_meshes.empty()) {
      cerr << "Loaded meshes:";
      for (auto &m : g_loaded_meshes)
        cerr << ' ' << m;
      cerr << '\n';
    }
    if (g_verbose.load() && !g_mesh_stats.empty()) {
      cerr << "Mesh load stats (verbose):\n";
      for (auto &s : g_mesh_stats) {
        cerr << "  " << s.name << ": " << s.triangles << " triangles, "
             << s.load_ms << " ms\n";
      }
    }
  }

  // Clamp tile_size to a sensible range
  if (tile_size <= 0)
    tile_size = 16;
  if (tile_size > pworld->GetImageWidth())
    tile_size = pworld->GetImageWidth();

  // Time the render
  auto renderStart = std::chrono::high_resolution_clock::now();

  render::RenderSceneToBitmap(*pworld, bitmap, threads, tile_size, tile_debug);

  auto renderEnd = std::chrono::high_resolution_clock::now();
  double renderTimeSeconds =
      std::chrono::duration<double>(renderEnd - renderStart).count();

  // Print render time
  if (!g_quiet.load()) {
    cerr << "\n=== Render Complete ===\n";
    cerr << "Total render time: " << std::fixed << std::setprecision(2)
         << renderTimeSeconds << " seconds\n";
    cerr << "Acceleration method: " << (useBVH ? "BVH" : "Linear") << "\n";
  }

  SaveImage(*pworld, outPath, bitmap);

  return 0;
}

void SaveImage(world &sceneWorld, const string &fileName,
               const vector<color> &bitmap) {
  // Image
  int W = sceneWorld.GetImageWidth();
  int H = sceneWorld.GetImageHeight();

  // Prepare 8-bit RGB buffer
  std::vector<unsigned char> img;
  img.reserve(W * H * 3);

  for (int j = 0; j < H; ++j) {
    for (int i = 0; i < W; ++i) {
      color pixel_color = bitmap[W * j + i];

      auto r = pixel_color.x();
      auto g = pixel_color.y();
      auto b = pixel_color.z();

      // Replace NaN components with zero.
      if (r != r)
        r = 0.0;
      if (g != g)
        g = 0.0;
      if (b != b)
        b = 0.0;

      auto scale = 1.0 / sceneWorld.GetSamplesPerPixel();
      r = sqrt(scale * r);
      g = sqrt(scale * g);
      b = sqrt(scale * b);

      unsigned char rc = static_cast<unsigned char>(256 * clamp(r, 0.0, 0.999));
      unsigned char gc = static_cast<unsigned char>(256 * clamp(g, 0.0, 0.999));
      unsigned char bc = static_cast<unsigned char>(256 * clamp(b, 0.0, 0.999));

      img.push_back(rc);
      img.push_back(gc);
      img.push_back(bc);
    }
  }

  // If filename ends with .png, write PNG using stbi_write_png, else write PPM
  auto ends_with = [](const string &s, const string &suffix) {
    if (s.size() < suffix.size())
      return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
                      [](char a, char b) { return tolower(a) == tolower(b); });
  };

  if (ends_with(fileName, ".png")) {
    int stride = W * 3;
    if (stbi_write_png(fileName.c_str(), W, H, 3, img.data(), stride)) {
      if (!g_quiet.load())
        cerr << "Saved PNG to " << fileName << "\n";
    } else {
      if (!g_quiet.load())
        cerr << "Failed to write PNG to " << fileName << "\n";
    }
  } else {
    // write PPM
    ofstream out(fileName, ios::binary);
    out << "P6\n" << W << ' ' << H << "\n255\n";
    out.write(reinterpret_cast<const char *>(img.data()), img.size());
    out.close();
    if (!g_quiet.load())
      cerr << "Saved PPM to " << fileName << "\n";
  }

  std::cerr << "\nDone.\n";
}
