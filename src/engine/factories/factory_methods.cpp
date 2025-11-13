
#include <iostream>
#include "factory_methods.h"
#include "../../defs.h"
#include "../world.h"
#include "../config.h"
#include "../camera.h"
#include "../sun.h"
#include "../sphere.h"
#include "../mesh.h"
#include "../lambertian.h"
#include "../metal.h"
#include "../emissive.h"
#include <chrono>
#include <filesystem>
#include "../../util/logging.h"

using namespace std;
using namespace tinyxml2;

// Definitions for diagnostics lists declared in the header
std::vector<std::string> g_attempted_meshes;
std::vector<std::string> g_loaded_meshes;
// Per-mesh load statistics (name, load time in ms, triangle count)
std::vector<MeshLoadInfo> g_mesh_stats;
// Scene directory for resolving relative mesh paths
std::string g_scene_directory;
shared_ptr<config> LoadConfig(XMLElement * configElem);
shared_ptr<camera> LoadCamera(XMLElement * cameraElem, float aspect_ratio);
shared_ptr<sun> LoadSun(XMLElement * lightsElem);
vector< shared_ptr<hittable> > LoadObjects(XMLElement * objectsElem);
shared_ptr<hittable> LoadSphere(XMLElement * sphereElem);
shared_ptr<hittable> LoadTriangle(XMLElement * triangleElem);
shared_ptr<hittable> LoadMesh(XMLElement * meshElem);
shared_ptr<material> LoadMaterial(string name);

shared_ptr<world> LoadScene(string fileName){
    XMLDocument doc;
    XMLError xmlErr = doc.LoadFile( fileName.c_str() );
    if(xmlErr != XML_SUCCESS)
    {
        cout << "Error parsing " << fileName << " -- error code: " << xmlErr;
        if(xmlErr == XML_ERROR_MISMATCHED_ELEMENT){
            cout << " -- Mismatched Element " <<  endl;
        }
        return shared_ptr<world>();
    }
    
    shared_ptr<world> pworld = make_shared<world>();

    // Record scene directory for resolving relative mesh filenames
    try{
        std::filesystem::path scenePath(fileName);
        if (scenePath.has_parent_path()) g_scene_directory = scenePath.parent_path().string();
        else g_scene_directory.clear();
    } catch(...){
        g_scene_directory.clear();
    }

    XMLNode * itemContainer = doc.FirstChildElement();
    if(!itemContainer){
        cerr << "LoadScene: missing root element in " << fileName << endl;
        return shared_ptr<world>();
    }

    XMLElement * configElem = itemContainer->FirstChildElement("Config");
    // Record the attempt for diagnostics
    g_attempted_meshes.push_back(fileName);
    if(!configElem){
        cerr << "LoadScene: missing <Config> element in " << fileName << endl;
        return shared_ptr<world>();
    }

    pworld->pconfig = LoadConfig(configElem);
    if(!pworld->pconfig){
        g_loaded_meshes.push_back(fileName);
        cerr << "LoadScene: failed to load <Config> from " << fileName << endl;
        return shared_ptr<world>();
    }

    XMLElement * cameraElem = configElem->NextSiblingElement("Camera");
    if(!cameraElem){
        cerr << "LoadScene: missing <Camera> element in " << fileName << endl;
        return shared_ptr<world>();
    }
    pworld->pcamera = LoadCamera(cameraElem, pworld->pconfig->ASPECT_RATIO);
    if(!pworld->pcamera){
        cerr << "LoadScene: failed to load <Camera> from " << fileName << endl;
        return shared_ptr<world>();
    }

    XMLElement * lightsElem = configElem->NextSiblingElement("Lights");
    if(!lightsElem){
        cerr << "LoadScene: missing <Lights> element in " << fileName << endl;
        return shared_ptr<world>();
    }
    pworld->psun = LoadSun(lightsElem);
    if(!pworld->psun){
        cerr << "LoadScene: failed to load <Lights> from " << fileName << endl;
        return shared_ptr<world>();
    }

    XMLElement * objectsElem = configElem->NextSiblingElement("Objects");
    if(!objectsElem){
        cerr << "LoadScene: missing <Objects> element in " << fileName << endl;
        return shared_ptr<world>();
    }
    pworld->objects = LoadObjects(objectsElem);

    return pworld;
}

shared_ptr<config> LoadConfig(XMLElement * configElem){

    if(!configElem){
        cerr << "LoadConfig: null config element" << endl;
        return shared_ptr<config>();
    }

    XMLElement * widthElem = configElem->FirstChildElement("Width");
    if(!widthElem || !widthElem->Attribute("value")){
        cerr << "LoadConfig: missing <Width value=...>" << endl;
        return shared_ptr<config>();
    }
    int width = atoi(widthElem->Attribute("value"));

    XMLElement * aspectElem = widthElem->NextSiblingElement("Aspect_ratio");
    if(!aspectElem || !aspectElem->Attribute("value")){
        cerr << "LoadConfig: missing <Aspect_ratio value=...>" << endl;
        return shared_ptr<config>();
    }
    float aspect_ratio = atof(aspectElem->Attribute("value"));

    XMLElement * samplesElem = widthElem->NextSiblingElement("Samples_Per_Pixel");
    if(!samplesElem || !samplesElem->Attribute("value")){
        cerr << "LoadConfig: missing <Samples_Per_Pixel value=...>" << endl;
        return shared_ptr<config>();
    }
    int samples = atoi(samplesElem->Attribute("value"));

    XMLElement * depthElem = widthElem->NextSiblingElement("Max_Depth");
    if(!depthElem || !depthElem->Attribute("value")){
        cerr << "LoadConfig: missing <Max_Depth value=...>" << endl;
        return shared_ptr<config>();
    }
    int depth = atoi(depthElem->Attribute("value"));

    shared_ptr<config> pconfig = make_shared<config>();
    pconfig->ASPECT_RATIO = aspect_ratio;
    pconfig->IMAGE_WIDTH = width;
    pconfig->IMAGE_HEIGHT = static_cast<int>(width / aspect_ratio);
    pconfig->SAMPLES_PER_PIXEL = samples;
    pconfig->MAX_DEPTH = depth;

    return pconfig;
}

shared_ptr<camera> LoadCamera(XMLElement * cameraElem, float aspect_ratio){

    XMLElement * vpWidthElem = cameraElem->FirstChildElement("Viewport_Width");
    float vpWidth = atof(vpWidthElem->Attribute("value"));
    
    XMLElement * focalElem = vpWidthElem->NextSiblingElement("Focal_Length");
    float focal_length = atof(focalElem->Attribute("value"));

    XMLElement * lookFromElem = vpWidthElem->NextSiblingElement("Look_From");
    float x,y,z;
    x = atof(lookFromElem->Attribute("x"));
    y = atof(lookFromElem->Attribute("y"));
    z = atof(lookFromElem->Attribute("z"));
    vec3 lookFrom(x,y,z);

    XMLElement * lookAtElem = vpWidthElem->NextSiblingElement("Look_at");
    x = atof(lookAtElem->Attribute("x"));
    y = atof(lookAtElem->Attribute("y"));
    z = atof(lookAtElem->Attribute("z"));
    vec3 lookAt(x,y,z);

    XMLElement * upElem = vpWidthElem->NextSiblingElement("Up");
    x = atof(upElem->Attribute("x"));
    y = atof(upElem->Attribute("y"));
    z = atof(upElem->Attribute("z"));
    vec3 up(x,y,z);

    XMLElement * fovElem = vpWidthElem->NextSiblingElement("FOV");
    float fov = atof(fovElem->Attribute("angle"));

    //XMLElement * aspectElem = vpWidthElem->NextSiblingElement("Aspect_ratio");
    //float aspectRatio = atof(aspectElem->Attribute("value"));

    shared_ptr<camera> pcamera = make_shared<camera>(lookFrom, lookAt, up, fov, aspect_ratio);
    pcamera->VIEWPORT_WIDTH = vpWidth;
    pcamera->VIEWPORT_HEIGHT = vpWidth / aspect_ratio;
    pcamera->ASPECT_RATIO = aspect_ratio;
    pcamera->FOCAL_LENGTH = focal_length;

    return pcamera;

}

shared_ptr<sun> LoadSun(XMLElement * lightsElem){
    
    XMLElement * sunElem = lightsElem->FirstChildElement("Sun");
    XMLElement * dirElem = sunElem->FirstChildElement("Direction");

    float x,y,z;
    x = atof(dirElem->Attribute("x"));
    y = atof(dirElem->Attribute("y"));
    z = atof(dirElem->Attribute("z"));
    vec3 dir(x,y,z);

    XMLElement * intensityElem = dirElem->NextSiblingElement("Intensity");
    float intensity = atof(intensityElem->Attribute("value"));

    float r,g,b;
    XMLElement * colorElem = dirElem->NextSiblingElement("Color");
    r = atof(colorElem->Attribute("r"));
    g = atof(colorElem->Attribute("g"));
    b = atof(colorElem->Attribute("b"));
    vec3 color(r,g,b);

    shared_ptr<sun> psun = make_shared<sun>();
    psun->direction = dir;
    psun->sunColor = color;

    return psun;

}

vector< shared_ptr<hittable> > LoadObjects(XMLElement * objectsElem){

    vector< shared_ptr<hittable> > list;

    XMLElement * item = objectsElem->FirstChildElement();
    while(item){
        string type = item->Name();
        if(type == "Sphere"){
            auto obj = LoadSphere(item);
            if(obj) list.push_back(obj);
        }
        else if(type == "Mesh"){
            auto obj = LoadMesh(item);
            if(obj) list.push_back(obj);
        }
        else if(type == "Triangle"){
            auto obj = LoadTriangle(item);
            if(obj) list.push_back(obj);
        }
        
        item = item->NextSiblingElement();
    }

    return list;

}

shared_ptr<hittable> LoadSphere(XMLElement * sphereElem){
    string name = sphereElem->Name();

    XMLElement * radiusElem = sphereElem->FirstChildElement("Radius");
    double radius = atof(radiusElem->Attribute("value"));

    XMLElement * posElem = radiusElem->NextSiblingElement("Position");
    float x,y,z;
    x = atof(posElem->Attribute("x"));
    y = atof(posElem->Attribute("y"));
    z = atof(posElem->Attribute("z"));
    vec3 center(x,y,z);

    XMLElement * scaleElem = radiusElem->NextSiblingElement("Scale");
    x = atof(scaleElem->Attribute("x"));
    y = atof(scaleElem->Attribute("y"));
    z = atof(scaleElem->Attribute("z"));
    vec3 scale(x,y,z);

    XMLElement * rotateElem = radiusElem->NextSiblingElement("Rotation");
    x = atof(rotateElem->Attribute("x"));
    y = atof(rotateElem->Attribute("y"));
    z = atof(rotateElem->Attribute("z"));
    vec3 rotation(x,y,z);

    XMLElement * materialElem = radiusElem->NextSiblingElement("Material");
    string materialName = materialElem->Attribute("name");

    auto material = LoadMaterial(materialName);
    shared_ptr<hittable> psphere = make_shared<sphere>(center, radius, material);

    return psphere;
}

shared_ptr<hittable> LoadTriangle(XMLElement * triangleElem){
    string name = triangleElem->Name();

    float x,y,z;
    XMLElement * v0Elem = triangleElem->FirstChildElement("V0");
    if(!v0Elem){
        cerr << "LoadTriangle: missing V0 element" << endl;
        return shared_ptr<hittable>();
    }
    x = atof(v0Elem->Attribute("x"));
    y = atof(v0Elem->Attribute("y"));
    z = atof(v0Elem->Attribute("z"));
    point3 v0(x,y,z);

    XMLElement * v1Elem = triangleElem->FirstChildElement("V1");
    if(!v1Elem){
        cerr << "LoadTriangle: missing V1 element" << endl;
        return shared_ptr<hittable>();
    }
    x = atof(v1Elem->Attribute("x"));
    y = atof(v1Elem->Attribute("y"));
    z = atof(v1Elem->Attribute("z"));
    point3 v1(x,y,z);

    XMLElement * v2Elem = triangleElem->FirstChildElement("V2");
    if(!v2Elem){
        cerr << "LoadTriangle: missing V2 element" << endl;
        return shared_ptr<hittable>();
    }
    x = atof(v2Elem->Attribute("x"));
    y = atof(v2Elem->Attribute("y"));
    z = atof(v2Elem->Attribute("z"));
    point3 v2(x,y,z);

    XMLElement * materialElem = triangleElem->FirstChildElement("Material");
    if(!materialElem){
        cerr << "LoadTriangle: missing Material element" << endl;
        return shared_ptr<hittable>();
    }
    string materialName = materialElem->Attribute("name");
    auto material = LoadMaterial(materialName);

    shared_ptr<hittable> ptriangle = make_shared<triangle>(vec3(v0.x(), v0.y(), v0.z()), vec3(v1.x(), v1.y(), v1.z()), vec3(v2.x(), v2.y(), v2.z()), material);
    return ptriangle;
}

shared_ptr<hittable> LoadMesh(XMLElement * meshElem){
    string name = meshElem->Name();

    XMLElement * posElem = meshElem->FirstChildElement("Position");
    float x,y,z;
    x = atof(posElem->Attribute("x"));
    y = atof(posElem->Attribute("y"));
    z = atof(posElem->Attribute("z"));
    vec3 position(x,y,z);

    XMLElement * scaleElem = posElem->NextSiblingElement("Scale");
    x = atof(scaleElem->Attribute("x"));
    y = atof(scaleElem->Attribute("y"));
    z = atof(scaleElem->Attribute("z"));
    vec3 scale(x,y,z);

    XMLElement * rotateElem = posElem->NextSiblingElement("Rotation");
    x = atof(rotateElem->Attribute("x"));
    y = atof(rotateElem->Attribute("y"));
    z = atof(rotateElem->Attribute("z"));
    vec3 rotation(x,y,z);

    XMLElement * materialElem = posElem->NextSiblingElement("Material");
    string materialName = materialElem->Attribute("name");
    auto material = LoadMaterial(materialName);

    XMLElement * fileElem = posElem->NextSiblingElement("File");
    string fileName = fileElem->Attribute("name");
    // We'll try a few candidate paths in order. Record each attempt for diagnostics.
    std::vector<std::string> candidates;
    candidates.push_back(fileName); // as provided
    // if scene directory known, try relative to it
    if (!g_scene_directory.empty()){
        std::filesystem::path p = std::filesystem::path(g_scene_directory) / fileName;
        candidates.push_back(p.string());
    }
    // try assets/ prefix as a common fallback
    candidates.push_back(std::string("assets/") + fileName);

    shared_ptr<hittable> pmesh = make_shared<mesh>(fileName, position, scale, rotation, material);
    shared_ptr<mesh> derived = dynamic_pointer_cast<mesh>(pmesh);

    // When trying multiple candidates, suppress mesh's own per-file prints so
    // we only show final results from the scene loader.
    bool prev_suppress = g_suppress_mesh_messages.load();
    g_suppress_mesh_messages = true;
    for (auto &candidate : candidates){
        // record attempt
        g_attempted_meshes.push_back(candidate);
        auto t0 = std::chrono::high_resolution_clock::now();
        if (derived->load(candidate)){
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            g_loaded_meshes.push_back(candidate);
            MeshLoadInfo info;
            info.name = candidate;
            info.load_ms = ms;
            info.triangles = derived->getTriangleCount();
            g_mesh_stats.push_back(info);
            return pmesh;
        }
    }
    g_suppress_mesh_messages = prev_suppress;

    // If the simple candidates failed, attempt a recursive search in a few
    // likely directories for a matching basename (this helps when the .obj
    // lives somewhere else in the tree). Stop after first match.
    std::string basename = std::filesystem::path(fileName).filename().string();
    std::vector<std::filesystem::path> search_roots;
    if (!g_scene_directory.empty()) search_roots.emplace_back(g_scene_directory);
    search_roots.emplace_back(std::filesystem::current_path());
    search_roots.emplace_back("assets");
    search_roots.emplace_back("build");
    search_roots.emplace_back("mesh");

    for (auto &root : search_roots){
        try{
            if (!std::filesystem::exists(root)) continue;
            int seen = 0;
            for (auto &ent : std::filesystem::recursive_directory_iterator(root)){
                if (!ent.is_regular_file()) continue;
                seen++;
                if (seen > 5000) break; // don't scan forever in very large trees
                if (ent.path().filename() == basename){
                    std::string found = ent.path().string();
                    g_attempted_meshes.push_back(found);
                    auto t0 = std::chrono::high_resolution_clock::now();
                    if (derived->load(found)){
                        auto t1 = std::chrono::high_resolution_clock::now();
                        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                        g_loaded_meshes.push_back(found);
                        MeshLoadInfo info;
                        info.name = found;
                        info.load_ms = ms;
                        info.triangles = derived->getTriangleCount();
                        g_mesh_stats.push_back(info);
                        return pmesh;
                    }
                    // if failed, continue searching
                }
            }
        }catch(...){}
    }

    cerr << "LoadMesh: failed to load mesh file '" << fileName << "' (tried ";
    for (size_t i = 0; i < candidates.size(); ++i){
        if (i) cerr << ", ";
        cerr << candidates[i];
    }
    cerr << ") and searched common directories" << endl;

    return shared_ptr<hittable>();
}

shared_ptr<material> LoadMaterial(string name){
    shared_ptr<material> pmaterial;
    
    if(name == "ground"){
        pmaterial = make_shared<lambertian>(color(0.8, 0.8, 0.0));
    }
    else if(name == "mattBrown"){
        pmaterial = make_shared<lambertian>(color(0.7, 0.3, 0.3));
    }
    else if(name == "fuzzySilver"){
        pmaterial = make_shared<metal>(color(0.8, 0.8, 0.8), 0.3);
    }else if(name == "shinyGold"){
        pmaterial = make_shared<metal>(color(0.8, 0.6, 0.2), 1.0);
    }
    else if(name == "emissive"){
        pmaterial = make_shared<emissive>(color(1.0, 1.0, 1.0));
    }

    return pmaterial;
}