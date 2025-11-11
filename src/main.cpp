#include <iostream>
#include <fstream>
#include <vector>
#include "defs.h"
#include "engine/factories/factory_methods.h"
#include "engine/world.h"
#include "engine/camera.h"
#include "engine/sun.h"
#include "engine/mesh.h"


using namespace std;

// Hittable List aka Scene:
hittable_list scene;

shared_ptr<world> pworld;


void SaveImage(string fileName, const vector<color> & bitmap);
void TestBitmap(vector<color> & bitmap);
//int testObjLoader();
color ray_color(const ray& r, int depth);

int main(int, char**) {
    // Resolve the scene XML from the assets directory.
    // Try a few sensible relative locations so running from the project root
    // or from the build directory both work.
    string candidates[] = {
        string("assets/objects.xml"),
        string("../assets/objects.xml"),
        string("./assets/objects.xml"),
        string("objects.xml")
    };

    string path;
    for (const auto &c : candidates) {
        std::ifstream f(c);
        if (f.good()) { path = c; break; }
    }
    if (path.empty()) {
        cerr << "Could not find objects.xml in assets/ (tried assets/objects.xml and ../assets/objects.xml)." << endl;
        return 2;
    }

    pworld = LoadScene(path);
    if (!pworld) {
        cerr << "Failed to load scene file: " << path << endl;
        return 3;
    }

    vector<color> bitmap;
    TestBitmap(bitmap);

    // Decide output location: write into the build directory as build/image.ppm
    // when running from the project root; when running from the build dir
    // write to image.ppm (which will be inside build/).
    #include <unistd.h>
    #include <libgen.h>
    char cwdBuf[4096];
    string outPath;
    if (getcwd(cwdBuf, sizeof(cwdBuf)) != nullptr) {
        string cwd(cwdBuf);
        // If running from the build directory itself, write to image.ppm
        if (!cwd.empty() && cwd.substr(cwd.find_last_of("/\\") + 1) == "build"){
            outPath = string("image.ppm");
        } else {
            outPath = string("build/image.ppm");
        }
    } else {
        // fallback
        outPath = string("build/image.ppm");
    }

    SaveImage(outPath, bitmap);

    return 0;
}


void TestBitmap(vector<color> & bitmap){

    shared_ptr<camera> pcamera = pworld->pcamera;

    for (int j = pworld->GetImageHeight() -1; j >= 0; --j) {
        std::cerr << "\rScanlines remaining: " << j << ' ' << std::flush;
        for (int i = 0; i < pworld->GetImageWidth(); ++i) {
            color pixel_color(0,0,0);
            for(int s = 0; s < pworld->GetSamplesPerPixel(); s++){
                auto u = (i + random_double()) / (pworld->GetImageWidth()-1);
                auto v = (j + random_double()) / (pworld->GetImageHeight()-1);
                ray r = pcamera->get_ray(u,v);
                pixel_color += ray_color(r, pworld->GetMaxDepth());
            }
            
            bitmap.push_back(pixel_color);
        }
    }
}

void SaveImage(string fileName, const vector<color> & bitmap){
    // Image

    // Save to file
    ofstream out(fileName);

    out << "P3\n" << pworld->GetImageWidth() << ' ' << pworld->GetImageHeight() << "\n255\n";

    for (int j= 0; j < pworld->GetImageHeight(); j++) {
        for (int i = 0; i < pworld->GetImageWidth(); ++i) {
            color pixel_color = bitmap[pworld->GetImageWidth()*j + i];

            auto r = pixel_color.x();
            auto g = pixel_color.y();
            auto b = pixel_color.z();

            // Replace NaN components with zero. See explanation in Ray Tracing: The Rest of Your Life.
            if (r != r) r = 0.0;
            if (g != g) g = 0.0;
            if (b != b) b = 0.0;

            // Divide the color by the number of samples and gamma-correct for gamma=2.0.
            auto scale = 1.0 / pworld->GetSamplesPerPixel();
            r = sqrt(scale * r);
            g = sqrt(scale * g);
            b = sqrt(scale * b);

            // Write the translated [0,255] value of each color component.
            out << static_cast<int>(256 * clamp(r, 0.0, 0.999)) << ' '
                << static_cast<int>(256 * clamp(g, 0.0, 0.999)) << ' '
                << static_cast<int>(256 * clamp(b, 0.0, 0.999)) << '\n';
                
        }
    }

    out.close();

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
