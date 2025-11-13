#include <iostream>
#include <fstream>
#include <vector>
#include "defs.h"
#include "engine/factories/factory_methods.h"
#include "engine/world.h"
#include "engine/config.h"
#include "engine/camera.h"
#include "engine/sun.h"
#include "engine/mesh.h"
#include <unistd.h>
#include <filesystem>
#include <thread>
#include <cstring>
#include <atomic>
#include <mutex>
#include <random>
#include <chrono>

// stb image write (minimal wrapper header)
#include "3rdParty/stb_image_write.h"
#include "util/logging.h"


using namespace std;

// Hittable List aka Scene:
hittable_list scene;

shared_ptr<world> pworld;


void SaveImage(string fileName, const vector<color> & bitmap);
void TestBitmap(vector<color> & bitmap, unsigned int threads, int tile_size, bool tile_debug);
//int testObjLoader();
color ray_color(const ray& r, int depth);

// Define logging flags
std::atomic<bool> g_quiet{false};
std::atomic<bool> g_verbose{false};
// Control to suppress per-file mesh open/failed messages during multi-path attempts
std::atomic<bool> g_suppress_mesh_messages{false};

int main(int argc, char** argv) {
    // Default CLI-controlled values
    string scenePath = "objects.xml";
    string outPathFlag;
    unsigned int threads = std::thread::hardware_concurrency();
    int tile_size = 64;
    bool tile_debug = false;
    int widthOverride = -1;
    int samplesOverride = -1;

    // Simple argv parser
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--scene" && i + 1 < argc) { scenePath = argv[++i]; }
        else if (a == "--out" && i + 1 < argc) { outPathFlag = argv[++i]; }
        else if (a == "--threads" && i + 1 < argc) { threads = (unsigned int)atoi(argv[++i]); }
    else if (a == "--tile-size" && i + 1 < argc) { tile_size = atoi(argv[++i]); }
    else if (a == "--tile-debug") { tile_debug = true; }
        else if (a == "--width" && i + 1 < argc) { widthOverride = atoi(argv[++i]); }
        else if (a == "--samples" && i + 1 < argc) { samplesOverride = atoi(argv[++i]); }
        else if (a == "-h" || a == "--help") {
            cout << "Usage: Raytracer [--scene <file>] [--out <file>] [--threads N] [--width W] [--samples S]" << endl;
            return 0;
        }
        else if (a == "--quiet") { g_quiet = true; }
        else if (a == "--verbose") { g_verbose = true; }
    }

    // Load scene file from current working directory (or as provided)
    std::ifstream sceneFile(scenePath);
    if (!sceneFile.good()){
        cerr << "Could not find scene file: " << scenePath << "\nPlease provide --scene <path> or place objects.xml in the working directory." << endl;
        return 2;
    }
    pworld = LoadScene(scenePath);
    if (!pworld) {
        cerr << "Failed to load scene file: " << scenePath << endl;
        return 3;
    }

    // Apply CLI overrides if provided
    if (widthOverride > 0) {
        pworld->pconfig->IMAGE_WIDTH = widthOverride;
        pworld->pconfig->IMAGE_HEIGHT = static_cast<int>(widthOverride / pworld->pconfig->ASPECT_RATIO);
    }
    if (samplesOverride > 0) {
        pworld->pconfig->SAMPLES_PER_PIXEL = samplesOverride;
    }

    vector<color> bitmap;

    // Decide output location: either CLI override or default behavior
    string outPath;
    char cwdBuf[4096];
    if (!outPathFlag.empty()) {
        outPath = outPathFlag;
    } else if (getcwd(cwdBuf, sizeof(cwdBuf)) != nullptr) {
        string cwd(cwdBuf);
        // If running from the build directory itself, write to image.ppm
        if (!cwd.empty() && cwd.substr(cwd.find_last_of("/\\") + 1) == "build"){
            outPath = string("image.png");
        } else {
            outPath = string("build/image.png");
        }
    } else {
        outPath = string("build/image.png");
    }

    // Startup summary (stderr) so it doesn't mix with progress output
    if (!g_quiet.load()){
        cerr << "Scene: " << scenePath << "\n";
        cerr << "Output: " << outPath << "\n";
        cerr << "Threads: " << threads << "\n";
        cerr << "Image size: " << pworld->pconfig->IMAGE_WIDTH << "x" << pworld->pconfig->IMAGE_HEIGHT << "\n";
        cerr << "Samples: " << pworld->pconfig->SAMPLES_PER_PIXEL << "\n";
        // Mesh diagnostics
        if (!g_attempted_meshes.empty()){
            cerr << "Attempted meshes:";
            for (auto &m : g_attempted_meshes) cerr << ' ' << m;
            cerr << '\n';
        }
        if (!g_loaded_meshes.empty()){
            cerr << "Loaded meshes:";
            for (auto &m : g_loaded_meshes) cerr << ' ' << m;
            cerr << '\n';
        }
        if (g_verbose.load() && !g_mesh_stats.empty()){
            cerr << "Mesh load stats (verbose):\n";
            for (auto &s : g_mesh_stats){
                cerr << "  " << s.name << ": " << s.triangles << " triangles, " << s.load_ms << " ms\n";
            }
        }
    }

    // Clamp tile_size to a sensible range
    if (tile_size <= 0) tile_size = 16;
    if (tile_size > pworld->GetImageWidth()) tile_size = pworld->GetImageWidth();

    TestBitmap(bitmap, threads, tile_size, tile_debug);

    SaveImage(outPath, bitmap);

    return 0;
}


void TestBitmap(vector<color> & bitmap, unsigned int threads, int tile_size, bool tile_debug){
    int W = pworld->GetImageWidth();
    int H = pworld->GetImageHeight();
    int samples = pworld->GetSamplesPerPixel();
    shared_ptr<camera> pcamera = pworld->pcamera;

    // Pre-size the bitmap so threads can write safely to indices without reallocation
    bitmap.clear();
    bitmap.resize(W * H);

    int TILE_SIZE = tile_size; // tile width/height (from CLI)

    // Per-tile statistics
    struct TileStat { int x0, y0, w, h; long long us; };
    std::vector<TileStat> tile_stats;
    std::mutex tile_stats_mtx;
    std::atomic<long long> total_tile_time_us{0};

    // Build tile list
    struct Tile { int x0, y0, w, h; };
    std::vector<Tile> tiles;
    for (int y = 0; y < H; y += TILE_SIZE){
        for (int x = 0; x < W; x += TILE_SIZE){
            int w = std::min(TILE_SIZE, W - x);
            int h = std::min(TILE_SIZE, H - y);
            tiles.push_back({x, y, w, h});
        }
    }

    std::atomic<size_t> next_tile{0};
    std::atomic<int> tiles_done{0};
    size_t total_tiles = tiles.size();
    std::mutex print_mtx;

    auto worker = [&](unsigned int thread_index){
        // Per-thread RNG
        std::mt19937 gen(static_cast<unsigned int>(std::hash<std::thread::id>{}(std::this_thread::get_id()) ^ (unsigned int)std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        std::uniform_real_distribution<double> dist(0.0, 1.0);

        while(true){
            size_t ti = next_tile.fetch_add(1);
            if (ti >= total_tiles) break;
            Tile t = tiles[ti];

            auto tile_t0 = std::chrono::high_resolution_clock::now();

            for (int yy = t.y0; yy < t.y0 + t.h; ++yy){
                for (int xx = t.x0; xx < t.x0 + t.w; ++xx){
                    color pixel_color(0,0,0);
                    for (int s = 0; s < samples; ++s){
                        double u = (xx + dist(gen)) / (W - 1);
                        double v = (yy + dist(gen)) / (H - 1);
                        ray r = pcamera->get_ray(u, v);
                        pixel_color += ray_color(r, pworld->GetMaxDepth());
                    }
                    // Keep the original program's row ordering: the single-threaded
                    // version pushed scanlines from top to bottom into the bitmap,
                    // so bitmap[0] corresponds to the top row. To preserve the same
                    // orientation, store into (H-1-yy) when yy is the image-row
                    // index with 0==bottom.
                    bitmap[(H - 1 - yy) * W + xx] = pixel_color;
                }
            }

            auto tile_t1 = std::chrono::high_resolution_clock::now();
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(tile_t1 - tile_t0).count();
            total_tile_time_us.fetch_add(us);
            {
                std::lock_guard<std::mutex> lk(tile_stats_mtx);
                tile_stats.push_back({t.x0, t.y0, t.w, t.h, us});
            }

            int done = ++tiles_done;
            if (!g_quiet.load()){
                double avg_us = done ? (double)total_tile_time_us.load() / (double)done : 0.0;
                size_t remaining = (total_tiles > (size_t)done) ? (total_tiles - (size_t)done) : 0;
                double est_remaining_ms = (avg_us * remaining) / 1000.0;
                std::lock_guard<std::mutex> lk(print_mtx);
                std::cerr << "\rTiles remaining: " << remaining << " | ETA: " << (est_remaining_ms/1000.0) << " s" << std::flush;
            }
        }
    };

    // Clamp thread count
    unsigned int nthreads = std::max<unsigned int>(1, std::min<unsigned int>(threads, std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : threads));
    std::vector<std::thread> pool;
    pool.reserve(nthreads);

    auto tstart = std::chrono::high_resolution_clock::now();
    for (unsigned int i = 0; i < nthreads; ++i) pool.emplace_back(worker, i);
    for (auto &th : pool) th.join();
    auto tend = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(tend - tstart).count();

    if (!g_quiet.load()){
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cerr << "\rTiles remaining: 0\n";
        std::cerr << "Render time: " << ms << " ms\n";
    }

    // Print per-tile stats summary when verbose (or when tile_debug requested)
    if (!g_quiet.load() && (g_verbose.load() || tile_debug)){
        std::lock_guard<std::mutex> lk(tile_stats_mtx);
        if (!tile_stats.empty()){
            long long min_us = tile_stats[0].us;
            long long max_us = tile_stats[0].us;
            long long sum_us = 0;
            for (auto &ts : tile_stats){
                if (ts.us < min_us) min_us = ts.us;
                if (ts.us > max_us) max_us = ts.us;
                sum_us += ts.us;
            }
            double avg_ms = (double)sum_us / (double)tile_stats.size() / 1000.0;
            double min_ms = (double)min_us / 1000.0;
            double max_ms = (double)max_us / 1000.0;
            std::cerr << "Tile stats: count=" << tile_stats.size() << ", avg=" << avg_ms << " ms, min=" << min_ms << " ms, max=" << max_ms << " ms\n";
            if (tile_debug){
                for (auto &ts : tile_stats){
                    std::cerr << "  tile(" << ts.x0 << "," << ts.y0 << ") " << ts.w << "x" << ts.h << ": " << (ts.us/1000.0) << " ms\n";
                }
            }
        }
    }
}

void SaveImage(string fileName, const vector<color> & bitmap){
    // Image
    int W = pworld->GetImageWidth();
    int H = pworld->GetImageHeight();

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
            if (r != r) r = 0.0;
            if (g != g) g = 0.0;
            if (b != b) b = 0.0;

            auto scale = 1.0 / pworld->GetSamplesPerPixel();
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
    auto ends_with = [](const string &s, const string &suffix){
        if (s.size() < suffix.size()) return false;
        return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(), [](char a, char b){ return tolower(a) == tolower(b); });
    };

    if (ends_with(fileName, ".png")){
        int stride = W * 3;
        if (stbi_write_png(fileName.c_str(), W, H, 3, img.data(), stride)){
            if (!g_quiet.load()) cerr << "Saved PNG to " << fileName << "\n";
        } else {
            if (!g_quiet.load()) cerr << "Failed to write PNG to " << fileName << "\n";
        }
    } else {
        // write PPM
        ofstream out(fileName, ios::binary);
        out << "P6\n" << W << ' ' << H << "\n255\n";
        out.write(reinterpret_cast<const char*>(img.data()), img.size());
        out.close();
        if (!g_quiet.load()) cerr << "Saved PPM to " << fileName << "\n";
    }

    std::cerr << "\nDone.\n";

}

color ray_color(const ray& r, int depth) {
    hit_record rec;
    
    if(depth <= 0)
        return color(0,0,0);

    if (pworld->hit(r, 0.001, INF, rec)) {
        ray scattered;
        color attenuation;
        color result;
        if (rec.mat_ptr->scatter(r, rec, attenuation, scattered)){
            result = attenuation * ray_color(scattered, depth-1);
        }
        else{
            result = color(0,0,0);
        }

        // Check for shadow:
        ray shadowRay;
        shadowRay.dir = pworld->psun->direction;
        shadowRay.orig = rec.p;
        if(pworld->hit(shadowRay, 0.001, INF, rec)){
            result = result * 0.2;
        }
        else{
            result = result * pworld->psun->sunColor;
        }

        return result;
    }
    vec3 unit_direction = unit_vector(r.direction());
    auto t = 0.5*(unit_direction.y() + 1.0);
    return (1.0-t)*color(1.0, 1.0, 1.0) + t*color(0.5, 0.7, 1.0);
}
