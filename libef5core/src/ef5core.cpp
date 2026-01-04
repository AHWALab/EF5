/*
 * EF5 Core Library - Common Utilities Implementation
 */

#include "ef5core/ef5core.hpp"
#include <cstdio>

#if _OPENMP
#include <omp.h>
#endif

namespace ef5 {

const char *get_version() {
  static char version_str[32];
  snprintf(version_str, sizeof(version_str), "%d.%d.%d", VERSION_MAJOR,
           VERSION_MINOR, VERSION_PATCH);
  return version_str;
}

int get_num_threads() {
#if _OPENMP
  return omp_get_max_threads();
#else
  return 1;
#endif
}

void set_num_threads(int n) {
#if _OPENMP
  if (n > 0) {
    omp_set_num_threads(n);
  }
#else
  (void)n; // Unused when OpenMP is disabled
#endif
}

} // namespace ef5
