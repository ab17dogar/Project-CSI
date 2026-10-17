#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

// Minimal self-contained test harness (no external dependencies, so the
// suite builds anywhere the renderer builds).

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace tf {

struct test_case {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<test_case> &registry() {
  static std::vector<test_case> tests;
  return tests;
}

inline int &failures() {
  static int n = 0;
  return n;
}

// Failure count when the current test started (set by the runner), so a
// test can stop early on its own failures without being cut short by an
// earlier test's.
inline int &test_start_failures() {
  static int n = 0;
  return n;
}

inline bool current_test_failed() { return failures() > test_start_failures(); }

inline int &checks() {
  static int n = 0;
  return n;
}

struct registrar {
  registrar(const char *name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
  }
};

inline void report_failure(const char *file, int line, const std::string &msg) {
  ++failures();
  std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str());
}

} // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)

#define TEST(name)                                                             \
  static void name();                                                          \
  static tf::registrar TF_CAT(name, _registrar)(#name, name);                  \
  static void name()

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    ++tf::checks();                                                            \
    if (!(cond)) {                                                             \
      std::ostringstream tf_os;                                                \
      tf_os << msg;                                                            \
      tf::report_failure(__FILE__, __LINE__, tf_os.str());                     \
    }                                                                          \
  } while (0)

#define CHECK_NEAR(a, b, tol, msg)                                             \
  do {                                                                         \
    const double tf_a = (a), tf_b = (b), tf_t = (tol);                         \
    ++tf::checks();                                                            \
    if (!(std::fabs(tf_a - tf_b) <= tf_t)) {                                   \
      std::ostringstream tf_os;                                                \
      tf_os << msg << " (got " << tf_a << ", expected " << tf_b               \
            << ", tol " << tf_t << ")";                                        \
      tf::report_failure(__FILE__, __LINE__, tf_os.str());                     \
    }                                                                          \
  } while (0)

#endif
