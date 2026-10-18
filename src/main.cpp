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
#include <cmath>
#include <cstdio>
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
#include "util/image_io.h"
#include "eval/progressive.h"
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

// "x,y,z" -> vec3. Rejects anything else, including trailing text.
bool ParseVec3(const std::string &text, vec3 &out) {
  double v[3];
  char tail;
  if (std::sscanf(text.c_str(), "%lf,%lf,%lf%c", &v[0], &v[1], &v[2], &tail) != 3)
    return false;
  out = vec3(v[0], v[1], v[2]);
  return true;
}

} // namespace

// Hittable List aka Scene:
hittable_list scene;

shared_ptr<world> pworld;

void SaveImage(world &sceneWorld, const string &fileName,
               const vector<color> &bitmap);
void SavePFM(world &sceneWorld, const string &fileName,
             const vector<color> &bitmap);
int RunProgressive(world &w, const string &scenePath, const string &outPath,
                   const string &pfmPath, unsigned threads, int tileSize,
                   double timeBudget, long long maxSpp,
                   const string &referencePath, const string &refVariancePath,
                   const string &logPath, const string &varianceOutPath,
                   bool logFlip, string label);

// Logging flags defined in util/logging.cpp

// Scale applied to radiance before the sRGB transfer in PNG output
// (--exposure, in stops). HDR outputs are never scaled.
static double g_display_exposure = 1.0;

int main(int argc, char **argv) {
  // Default CLI-controlled values
  string scenePath = "objects.xml";
  string outPathFlag;
  unsigned int threads = std::thread::hardware_concurrency();
  int tile_size = 64;
  bool tile_debug = false;
  int widthOverride = -1;
  int samplesOverride = -1;
  int accelOverride = -1; // -1: scene default (BVH), 0: linear, 1: BVH
  bool useDenoiser = true;
  string pfmPath;
  int maxDepthOverride = -1;
  int rrDepthOverride = -1;
  long long seedOverride = -1;
  string integratorName;
  // Progressive / measurement mode
  bool progressive = false;
  double timeBudget = 0.0;
  string referencePath, refVariancePath, logPath, varianceOutPath, label;
  bool logFlip = true;
  // Camera overrides (multi-view dataset export) and geometry output
  string lookFromArg, lookAtArg, upArg, geometryPrefix;
  double fovOverride = -1.0;
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
      accelOverride = 1;
    } else if (a == "--linear") {
      accelOverride = 0;
    } else if (a == "--pfm" && i + 1 < argc) {
      pfmPath = argv[++i];
    } else if (a == "--progressive") {
      progressive = true;
    } else if (a == "--time-budget" && i + 1 < argc) {
      timeBudget = atof(argv[++i]);
      progressive = true;
    } else if (a == "--reference" && i + 1 < argc) {
      referencePath = argv[++i];
      progressive = true;
    } else if (a == "--ref-variance" && i + 1 < argc) {
      refVariancePath = argv[++i];
    } else if (a == "--log" && i + 1 < argc) {
      logPath = argv[++i];
      progressive = true;
    } else if (a == "--variance-out" && i + 1 < argc) {
      varianceOutPath = argv[++i];
      progressive = true;
    } else if (a == "--no-flip") {
      logFlip = false;
    } else if (a == "--label" && i + 1 < argc) {
      label = argv[++i];
    } else if (a == "--look-from" && i + 1 < argc) {
      lookFromArg = argv[++i];
    } else if (a == "--look-at" && i + 1 < argc) {
      lookAtArg = argv[++i];
    } else if (a == "--up" && i + 1 < argc) {
      upArg = argv[++i];
    } else if (a == "--fov" && i + 1 < argc) {
      fovOverride = atof(argv[++i]);
    } else if (a == "--exposure" && i + 1 < argc) {
      g_display_exposure = std::pow(2.0, atof(argv[++i]));
    } else if (a == "--geometry-out" && i + 1 < argc) {
      geometryPrefix = argv[++i];
    } else if (a == "--max-depth" && i + 1 < argc) {
      maxDepthOverride = atoi(argv[++i]);
    } else if (a == "--rr-depth" && i + 1 < argc) {
      rrDepthOverride = atoi(argv[++i]);
    } else if (a == "--seed" && i + 1 < argc) {
      seedOverride = atoll(argv[++i]);
    } else if (a == "--integrator" && i + 1 < argc) {
      integratorName = argv[++i];
      if (integratorName != "mis" && integratorName != "bsdf" &&
          integratorName != "nee") {
        cerr << "Unknown integrator '" << integratorName
             << "' (expected mis, bsdf or nee)" << endl;
        return 4;
      }
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
          << "  --bvh            Use BVH acceleration (default)\n"
          << "  --linear         Use linear traversal (slow; for testing)\n"
          << "  --integrator X   Light transport: mis (default), bsdf, nee\n"
          << "  --max-depth N    Maximum bounces (1 = direct lighting)\n"
          << "  --rr-depth N     Bounces before Russian roulette\n"
          << "  --seed N         Random seed (same seed -> same image)\n"
          << "  --pfm <file>     Also write linear HDR radiance (.pfm)\n"
          << "  --look-from X,Y,Z  Override the camera position\n"
          << "  --look-at X,Y,Z    Override the camera target\n"
          << "  --up X,Y,Z         Override the camera up vector\n"
          << "  --fov DEG          Override the vertical field of view\n"
          << "  --exposure STOPS   Exposure of the PNG output (HDR output unchanged)\n"
          << "  --geometry-out P   Write P_depth.pfm (z-depth) and P_normal.pfm\n"
          << "                     (world space) of the first hit per pixel\n"
          << "\nMeasurement (progressive rendering, no denoising):\n"
          << "  --time-budget S  Render for S seconds (render time only)\n"
          << "  --progressive    Passes of doubling spp up to --samples\n"
          << "  --reference F    Reference .pfm: log error after every pass\n"
          << "  --ref-variance F Reference variance .pfm (noise correction)\n"
          << "  --log F          Per-pass CSV (time, spp, rays, MSE, FLIP)\n"
          << "  --variance-out F Per-pixel variance of the mean (.pfm)\n"
          << "  --no-flip        Skip FLIP in the per-pass log\n"
          << "  --label NAME     Method label in the log (default: integrator)\n"
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

  // Apply acceleration / integrator overrides
  if (accelOverride == 1) {
    pworld->pconfig->acceleration = AccelerationMethod::BVH;
  } else if (accelOverride == 0) {
    pworld->pconfig->acceleration = AccelerationMethod::LINEAR;
  }
  const bool useBVH =
      pworld->pconfig->acceleration == AccelerationMethod::BVH;
  if (maxDepthOverride >= 0)
    pworld->pconfig->MAX_DEPTH = maxDepthOverride;
  if (rrDepthOverride >= 0)
    pworld->pconfig->rouletteDepth = rrDepthOverride;
  if (seedOverride >= 0)
    pworld->pconfig->seed = static_cast<uint64_t>(seedOverride);
  if (integratorName == "bsdf")
    pworld->pconfig->lightStrategy = LightStrategy::BSDF;
  else if (integratorName == "nee")
    pworld->pconfig->lightStrategy = LightStrategy::NEE;
  else if (integratorName == "mis")
    pworld->pconfig->lightStrategy = LightStrategy::MIS;

  // Apply denoiser setting
  pworld->pconfig->enableDenoiser = useDenoiser;

  // Camera overrides keep the scene's lens (aperture, focus distance).
  if (!lookFromArg.empty() || !lookAtArg.empty() || !upArg.empty() ||
      fovOverride > 0.0) {
    const camera &old = *pworld->pcamera;
    point3 from = old.LOOK_FROM, at = old.LOOK_AT;
    vec3 up = old.UP;
    if ((!lookFromArg.empty() && !ParseVec3(lookFromArg, from)) ||
        (!lookAtArg.empty() && !ParseVec3(lookAtArg, at)) ||
        (!upArg.empty() && !ParseVec3(upArg, up))) {
      cerr << "Camera vectors must be given as X,Y,Z" << endl;
      return 4;
    }
    pworld->pcamera = std::make_shared<camera>(
        from, at, up, fovOverride > 0.0 ? fovOverride : old.FOV,
        pworld->pconfig->ASPECT_RATIO, old.aperture, old.focus_dist);
  }

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
    cerr << "Integrator: "
         << (pworld->pconfig->lightStrategy == LightStrategy::MIS    ? "mis"
             : pworld->pconfig->lightStrategy == LightStrategy::BSDF ? "bsdf"
                                                                     : "nee")
         << ", max depth " << pworld->pconfig->MAX_DEPTH << ", seed "
         << pworld->pconfig->seed << "\n";
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

  if (!geometryPrefix.empty()) {
    const render::GeometryBuffers g = render::RenderGeometry(*pworld);
    if (!write_pfm(geometryPrefix + "_depth.pfm", g.depth) ||
        !write_pfm(geometryPrefix + "_normal.pfm", g.normal)) {
      cerr << "Failed to write geometry buffers " << geometryPrefix << "_*.pfm"
           << endl;
      return 5;
    }
  }

  if (progressive)
    return RunProgressive(*pworld, resolvedScenePath, outPath, pfmPath,
                          threads, tile_size, timeBudget,
                          samplesOverride > 0 ? samplesOverride : 0,
                          referencePath, refVariancePath, logPath,
                          varianceOutPath, logFlip, label);

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
  if (!pfmPath.empty())
    SavePFM(*pworld, pfmPath, bitmap);

  return 0;
}

// Portable float map: linear mean radiance per pixel. This is the format
// used for error measurements against reference renders.
void SavePFM(world &sceneWorld, const string &fileName,
             const vector<color> &bitmap) {
  render::RenderBuffers buffers;
  buffers.width = sceneWorld.GetImageWidth();
  buffers.height = sceneWorld.GetImageHeight();
  buffers.spp = sceneWorld.GetSamplesPerPixel();
  buffers.sum = bitmap;
  if (write_pfm(fileName, buffers.mean())) {
    if (!g_quiet.load())
      cerr << "Saved PFM to " << fileName << "\n";
  } else {
    cerr << "Failed to write PFM to " << fileName << "\n";
  }
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

      // Mean radiance, then the sRGB transfer function (the old sqrt was a
      // gamma-2.0 approximation).
      auto scale = g_display_exposure / sceneWorld.GetSamplesPerPixel();
      r = linear_to_srgb(scale * r);
      g = linear_to_srgb(scale * g);
      b = linear_to_srgb(scale * b);

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

  if (!g_quiet.load())
    std::cerr << "\nDone.\n";
}

// Progressive / equal-time rendering with optional per-pass error logging.
int RunProgressive(world &w, const string &scenePath, const string &outPath,
                   const string &pfmPath, unsigned threads, int tileSize,
                   double timeBudget, long long maxSpp,
                   const string &referencePath, const string &refVariancePath,
                   const string &logPath, const string &varianceOutPath,
                   bool logFlip, string label) {
  namespace fs = std::filesystem;
  const LightStrategy strategy = w.pconfig->lightStrategy;
  if (label.empty())
    label = strategy == LightStrategy::MIS    ? "mis"
            : strategy == LightStrategy::BSDF ? "bsdf"
                                              : "nee";
  const string sceneName = fs::path(scenePath).stem().string();

  image_rgb reference, refVariance;
  string err;
  if (!referencePath.empty() && !read_pfm(referencePath, reference, &err)) {
    cerr << "Cannot read reference: " << err << "\n";
    return 2;
  }
  if (!refVariancePath.empty() &&
      !read_pfm(refVariancePath, refVariance, &err)) {
    cerr << "Cannot read reference variance: " << err << "\n";
    return 2;
  }

  std::ofstream log;
  if (!logPath.empty()) {
    fs::path lp(logPath);
    if (lp.has_parent_path()) {
      std::error_code ec;
      fs::create_directories(lp.parent_path(), ec);
    }
    log.open(logPath);
    if (!log) {
      cerr << "Cannot write log " << logPath << "\n";
      return 2;
    }
    log << "# scene=" << scenePath << " method=" << label
        << " width=" << w.GetImageWidth() << " height=" << w.GetImageHeight()
        << " max_depth=" << w.GetMaxDepth() << " seed=" << w.pconfig->seed
        << " threads=" << threads << " time_budget=" << timeBudget
        << " max_spp=" << maxSpp << " reference=" << referencePath << "\n";
    log << eval::csv_header() << "\n";
  }

  eval::progressive_settings ps;
  ps.time_budget = timeBudget;
  ps.max_spp = timeBudget > 0.0 ? maxSpp
                                : (maxSpp > 0 ? maxSpp : w.GetSamplesPerPixel());
  ps.threads = threads;
  ps.tile_size = tileSize;
  ps.reference = referencePath.empty() ? nullptr : &reference;
  ps.reference_variance = refVariancePath.empty() ? nullptr : &refVariance;
  ps.flip = logFlip;
  ps.on_pass = [&](const eval::pass_record &p) {
    if (log.is_open())
      log << eval::csv_row(p, sceneName, label) << "\n" << std::flush;
    if (!g_quiet.load()) {
      cerr << "pass " << p.pass << ": " << p.spp << " spp, " << std::fixed
           << std::setprecision(3) << p.total_seconds << " s";
      if (p.has_metrics) {
        cerr << std::scientific << std::setprecision(4)
             << ", relMSE " << p.error.relmse;
        if (p.error.flip >= 0.0)
          cerr << std::fixed << std::setprecision(4) << ", FLIP "
               << p.error.flip;
      }
      cerr << std::defaultfloat << "\n";
    }
  };

  eval::progressive_result result;
  try {
    result = eval::render_progressive(w, ps);
  } catch (const std::exception &e) {
    cerr << "Progressive render failed: " << e.what() << "\n";
    return 2;
  }

  const eval::pass_record &last = result.passes.back();
  if (!g_quiet.load()) {
    const double rays = static_cast<double>(last.camera_rays + last.bounce_rays +
                                            last.shadow_rays);
    cerr << "Rendered " << last.spp << " spp in " << last.total_seconds
         << " s (" << rays / last.total_seconds / 1e6 << " Mrays/s)\n";
    const long long bad = render::InvalidSampleCount();
    if (bad > 0)
      cerr << "WARNING: " << bad
           << " samples returned NaN/Inf and were discarded\n";
  }

  w.pconfig->SAMPLES_PER_PIXEL = static_cast<int>(result.buffers.spp);
  SaveImage(w, outPath, result.buffers.sum);
  if (!pfmPath.empty())
    SavePFM(w, pfmPath, result.buffers.sum);
  if (!varianceOutPath.empty()) {
    if (write_pfm(varianceOutPath, result.buffers.variance_of_mean())) {
      if (!g_quiet.load())
        cerr << "Saved variance to " << varianceOutPath << "\n";
    } else {
      cerr << "Failed to write " << varianceOutPath << "\n";
      return 2;
    }
  }
  return 0;
}
