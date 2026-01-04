/*
 * EF5 Core Library - Type Definitions
 *
 * Core data types used across all EF5 models.
 * Designed for compatibility with NumPy arrays via contiguous memory layouts.
 */

#ifndef EF5CORE_TYPES_HPP
#define EF5CORE_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ef5 {

// ============================================================================
// Grid Cell Types
// ============================================================================

/**
 * @brief Basic grid node containing topographic and connectivity information.
 *
 * This structure holds per-cell properties that are constant during simulation.
 * Arrays of these are typically pre-computed from DEM processing.
 */
struct GridCell {
  // Grid indices
  int32_t x; ///< Column index in grid
  int32_t y; ///< Row index in grid

  // Topographic properties
  float slope;        ///< Terrain slope (dimensionless, rise/run)
  float area;         ///< Cell area in km²
  float contrib_area; ///< Contributing area in km²
  float hor_len;      ///< Horizontal length through cell in meters
  float river_len;    ///< River length through cell in meters
  float relief;       ///< Relief in meters

  // Flow accumulation
  int64_t fac; ///< Flow accumulation count

  // Connectivity
  int64_t downstream_index; ///< Index of downstream cell, -1 if outlet
  bool is_channel;          ///< True if cell is a channel cell
};

// Invalid downstream node marker
constexpr int64_t INVALID_DOWNSTREAM = -1;

// ============================================================================
// Routing Topology
// ============================================================================

/**
 * @brief Routing topology for parallel kinematic wave computation.
 *
 * Cells are grouped into levels where cells at the same level have no
 * dependencies on each other and can be processed in parallel.
 * Level 0 = headwater cells (no upstream contributions)
 * Level N = outlet (all others upstream)
 */
struct RoutingTopology {
  std::vector<int32_t>
      level_starts; ///< Index where each level begins in sorted_indices
  std::vector<int32_t>
      sorted_indices; ///< Cell indices sorted by level (headwaters first)
  int32_t num_levels; ///< Total number of levels

  /**
   * @brief Compute routing topology from grid connectivity.
   *
   * @param cells Array of grid cells with downstream connectivity
   * @param n_cells Number of cells
   */
  void compute_from_grid(const GridCell *cells, size_t n_cells);
};

// ============================================================================
// Array View Types (for NumPy interop)
// ============================================================================

/**
 * @brief Non-owning view into a contiguous array.
 *
 * Used for passing NumPy arrays to C++ without copying data.
 * Python bindings convert py::array_t<T> to ArrayView<T>.
 */
template <typename T> struct ArrayView {
  T *data;
  size_t size;

  ArrayView() : data(nullptr), size(0) {}
  ArrayView(T *d, size_t s) : data(d), size(s) {}

  T &operator[](size_t i) { return data[i]; }
  const T &operator[](size_t i) const { return data[i]; }

  T *begin() { return data; }
  T *end() { return data + size; }
  const T *begin() const { return data; }
  const T *end() const { return data + size; }
};

using FloatArray = ArrayView<float>;
using ConstFloatArray = ArrayView<const float>;

// ============================================================================
// Common Utilities
// ============================================================================

/**
 * @brief Clamp a value to a range.
 */
template <typename T> inline T clamp(T value, T min_val, T max_val) {
  return (value < min_val) ? min_val : ((value > max_val) ? max_val : value);
}

/**
 * @brief Check if a floating point value is finite (not NaN or Inf).
 */
inline bool is_finite(float x) {
  return (x == x) && (x != x + 1.0f || x == 0.0f);
}

} // namespace ef5

#endif // EF5CORE_TYPES_HPP
