// Correctness test runner. Usage: raytracer_tests [substring-filter]
#include "test_framework.h"
#include "util/logging.h"

#include <chrono>
#include <cstring>

int main(int argc, char **argv) {
  g_quiet = true;
  const char *filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0;
  for (const auto &t : tf::registry()) {
    if (filter && t.name.find(filter) == std::string::npos)
      continue;
    const int before = tf::failures();
    tf::test_start_failures() = before;
    std::printf("[ RUN  ] %s\n", t.name.c_str());
    std::fflush(stdout);
    const auto t0 = std::chrono::steady_clock::now();
    t.fn();
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    std::printf("[ %s ] %s (%.0f ms)\n",
                tf::failures() == before ? " OK " : "FAIL", t.name.c_str(),
                ms);
    std::fflush(stdout);
    ++ran;
  }
  std::printf("\n%d tests, %d checks, %d failures\n", ran, tf::checks(),
              tf::failures());
  return tf::failures() == 0 && ran > 0 ? 0 : 1;
}
