#ifndef LOGGING_H
#define LOGGING_H

#include <atomic>

// Global logging flags controlled from main().
extern std::atomic<bool> g_quiet;
extern std::atomic<bool> g_verbose;
// When true, mesh loader will suppress per-file open/failed messages. This
// is used by the scene loader which may attempt several candidate paths.
extern std::atomic<bool> g_suppress_mesh_messages;

#endif // LOGGING_H
