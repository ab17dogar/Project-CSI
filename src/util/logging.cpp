#include "util/logging.h"

std::atomic<bool> g_quiet{false};
std::atomic<bool> g_verbose{false};
std::atomic<bool> g_suppress_mesh_messages{false};
