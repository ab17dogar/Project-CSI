#ifndef FACTORY_METHODS_H
#define FACTORY_METHODS_H

#include <memory> // for std::shared_ptr

#include <string>
#include <vector>

// Lists populated by the scene loader for diagnostics. The definitions are in
// factory_methods.cpp and are safe to read after calling LoadScene().
extern std::vector<std::string> g_attempted_meshes;
extern std::vector<std::string> g_loaded_meshes;

// Directory containing the last-loaded scene XML. If non-empty, mesh loaders
// will try filenames relative to this directory as a fallback.
extern std::string g_scene_directory;

struct MeshLoadInfo {
	std::string name;
	double load_ms;
	int triangles;
};

extern std::vector<MeshLoadInfo> g_mesh_stats;

class world;

std::shared_ptr<world> LoadScene(std::string fileName);


#endif
