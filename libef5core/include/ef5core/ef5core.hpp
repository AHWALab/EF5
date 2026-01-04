/*
 * EF5 Core Library - Main Header
 *
 * This library contains the computational kernels for EF5 hydrological
 * modeling. All I/O operations and configuration are handled by the Python
 * wrapper.
 *
 * Design Principles:
 * 1. Pure computation - no file I/O, no global state
 * 2. NumPy-compatible array interfaces via contiguous float* buffers
 * 3. OpenMP parallelization for grid operations
 * 4. SIMD-friendly data layouts where practical
 */

#ifndef EF5CORE_HPP
#define EF5CORE_HPP

#include "ef5core/crest.hpp"
#include "ef5core/hymod.hpp"
#include "ef5core/kinematic.hpp"
#include "ef5core/linear_route.hpp"
#include "ef5core/sac.hpp"
#include "ef5core/snow17.hpp"
#include "ef5core/types.hpp"

namespace ef5 {

// Library version
constexpr int VERSION_MAJOR = 2;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 0;

// Get version string
const char *get_version();

// Get number of OpenMP threads
int get_num_threads();

// Set number of OpenMP threads
void set_num_threads(int n);

} // namespace ef5

#endif // EF5CORE_HPP
