/*
 * EF5 Core Library - Linear Reservoir Routing Model
 *
 * Simple linear reservoir routing (Muskingum-Cunge style).
 * To be fully implemented - placeholder for build system.
 */

#ifndef EF5CORE_LINEAR_ROUTE_HPP
#define EF5CORE_LINEAR_ROUTE_HPP

#include "types.hpp"
#include <cstddef>

namespace ef5 {
namespace linear_route {

struct Parameters {
  float k; // Storage coefficient (hours)
  float x; // Weighting factor (0-0.5)

  Parameters() : k(1.0f), x(0.2f) {}
};

struct State {
  float prev_q; // Previous discharge

  State() : prev_q(0.0f) {}
};

// Placeholder - full implementation to follow
void route_cell(const Parameters &params, State &state, float inflow,
                float step_hours, float &outflow);

void route_grid(const GridCell *cells, const Parameters *params, State *states,
                const float *lateral_inflow, float *discharge, size_t n_cells,
                float step_hours, const int32_t *sorted_indices);

} // namespace linear_route
} // namespace ef5

#endif // EF5CORE_LINEAR_ROUTE_HPP
