/*
 * EF5 Core Library - Kinematic Wave Routing Implementation
 */

#include "ef5core/kinematic.hpp"
#include <algorithm>
#include <cmath>

#if _OPENMP
#include <omp.h>
#endif

namespace ef5 {
namespace kinematic {

float newton_raphson_solve(float dt_over_dx, float alpha, float beta,
                           float incoming_q, float prev_q, float lateral_in,
                           float step_seconds) {
  // Compute backward difference term for stability
  float back_diff_q = 0.0f;
  if (incoming_q + prev_q > 0.0f) {
    back_diff_q = std::pow((incoming_q + prev_q) / 2.0f, beta - 1.0f);
    if (!std::isfinite(back_diff_q)) {
      back_diff_q = 0.0f;
    }
  }

  // Build equation coefficients
  // Equation: (Δt/Δx)·Q + α·Q^β = RHS
  float A = dt_over_dx * incoming_q;
  float B = alpha * beta * prev_q * back_diff_q;
  float C = step_seconds * lateral_in;
  float D = dt_over_dx;
  float E = alpha * beta * back_diff_q;

  // Initial estimate using linear approximation
  float est_q = (E + D > 0.0f) ? (A + B + C) / (D + E) : 0.0f;

  // Right-hand side of the equation
  float rhs = A + alpha * std::pow(prev_q, beta) + step_seconds * lateral_in;

  // Newton-Raphson iteration
  constexpr int MAX_ITER = 10;
  constexpr float TOLERANCE = 0.01f;

  for (int iter = 0; iter < MAX_ITER; iter++) {
    // Residual: f(Q) = (Δt/Δx)·Q + α·Q^β - RHS
    float residual = dt_over_dx * est_q + alpha * std::pow(est_q, beta) - rhs;

    if (!std::isfinite(residual)) {
      residual = 0.0f;
    }

    if (std::fabs(residual) < TOLERANCE) {
      break;
    }

    // Derivative: f'(Q) = Δt/Δx + α·β·Q^(β-1)
    float derivative = dt_over_dx + alpha * beta * std::pow(est_q, beta - 1.0f);

    if (!std::isfinite(derivative) || derivative == 0.0f) {
      derivative = 1.0f;
    }

    // Newton-Raphson update
    est_q = est_q - residual / derivative;

    // Ensure non-negative
    if (est_q < 0.0f) {
      est_q = 0.0f;
    }
  }

  return (est_q >= 0.0f) ? est_q : 0.0f;
}

void route_overland_cell(const Parameters &params, State &state,
                         const GridCell &cell, float fast_flow, float slow_flow,
                         float step_seconds, int64_t downstream_idx,
                         State *all_states) {
  const float beta = 0.6f; // Fixed for overland flow
  const float alpha = params.alpha0;

  // Convert fast_flow from m/s to m
  float lateral_in = fast_flow / 1000.0f;

  // Solve kinematic wave equation
  float dt_over_dx = step_seconds / cell.hor_len;

  float new_q =
      newton_raphson_solve(dt_over_dx, alpha, beta,
                           static_cast<float>(state.incoming_water_overland),
                           state.prev_channel_q, lateral_in, step_seconds);

  // Update state
  state.prev_channel_q = new_q;

  // Pass flow downstream
  if (downstream_idx != INVALID_DOWNSTREAM && all_states != nullptr) {
    all_states[downstream_idx].incoming_water_overland += new_q;
  }

  state.incoming_fastflow = new_q;

  // Handle interflow reservoir
  state.interflow_reservoir += slow_flow;
  float interflow_leak = state.interflow_reservoir * params.leak_i;
  state.interflow_reservoir -= interflow_leak;
  if (state.interflow_reservoir < 0.0f) {
    state.interflow_reservoir = 0.0f;
  }

  state.incoming_interflow = interflow_leak;
}

void route_channel_cell(const Parameters &params, State &state,
                        const GridCell &cell, float fast_flow, float slow_flow,
                        float step_seconds, int64_t downstream_idx,
                        State *all_states) {
  // First: overland routing component
  const float beta_overland = 0.6f;
  const float alpha_overland = params.alpha0;

  // Add interflow to slow_flow for channel cells
  float total_slow = slow_flow + static_cast<float>(state.incoming_interflow);

  // Convert to meters
  float fast_in = fast_flow / 1000.0f;
  float slow_in = total_slow / 1000.0f;
  float lateral_in = fast_in + slow_in;

  float dt_over_dx = step_seconds / cell.hor_len;

  // Solve overland component
  float new_overland_q =
      newton_raphson_solve(dt_over_dx, alpha_overland, beta_overland,
                           static_cast<float>(state.incoming_water_overland),
                           state.prev_overland_q, lateral_in, step_seconds);

  state.prev_overland_q = new_overland_q;

  // Second: channel routing
  const float beta_channel = params.beta;
  const float alpha_channel = params.alpha;

  float new_channel_q = newton_raphson_solve(
      dt_over_dx, alpha_channel, beta_channel,
      static_cast<float>(state.incoming_water_channel), state.prev_channel_q,
      new_overland_q, // Overland output becomes channel lateral input
      step_seconds);

  state.prev_channel_q = new_channel_q;

  // Pass flow downstream
  if (downstream_idx != INVALID_DOWNSTREAM && all_states != nullptr) {
    all_states[downstream_idx].incoming_water_channel += new_channel_q;
  }

  state.incoming_fastflow = new_channel_q;
  state.incoming_interflow = 0.0;
}

void route_grid(const GridCell *cells, const Parameters *params, State *states,
                const float *fast_flow, const float *slow_flow,
                float *discharge, size_t n_cells, float step_hours,
                const int32_t *sorted_indices) {
  float step_seconds = step_hours * 3600.0f;

  // Process cells in upstream-to-downstream order
  // sorted_indices should be reversed (outlet first) based on original EF5
  // logic
  for (size_t order = 0; order < n_cells; order++) {
    size_t i = (sorted_indices != nullptr)
                   ? static_cast<size_t>(sorted_indices[n_cells - 1 - order])
                   : (n_cells - 1 - order);

    const GridCell &cell = cells[i];

    if (!cell.is_channel) {
      route_overland_cell(params[i], states[i], cell, fast_flow[i],
                          slow_flow[i], step_seconds, cell.downstream_index,
                          states);
    } else {
      route_channel_cell(params[i], states[i], cell, fast_flow[i], slow_flow[i],
                         step_seconds, cell.downstream_index, states);
    }
  }

  // Compute final discharge output
  for (size_t i = 0; i < n_cells; i++) {
    const GridCell &cell = cells[i];

    if (!cell.is_channel) {
      // Overland cell: combine fast and interflow
      float q = static_cast<float>(states[i].incoming_fastflow) * cell.hor_len;
      q += static_cast<float>(states[i].incoming_interflow) * cell.area / 3.6f;
      discharge[i] = q;
    } else {
      // Channel cell: direct channel discharge
      discharge[i] = static_cast<float>(states[i].incoming_fastflow);
    }
  }
}

void route_grid_parallel(const GridCell *cells, const Parameters *params,
                         State *states, const float *fast_flow,
                         const float *slow_flow, float *discharge,
                         size_t n_cells, float step_hours,
                         const RoutingTopology &topology) {
  float step_seconds = step_hours * 3600.0f;

  // Process level by level (sequential between levels, parallel within)
  for (int32_t level = 0; level < topology.num_levels; level++) {
    int32_t start_idx = topology.level_starts[level];
    int32_t end_idx =
        (level + 1 < topology.num_levels)
            ? topology.level_starts[level + 1]
            : static_cast<int32_t>(topology.sorted_indices.size());

#if _OPENMP
#pragma omp parallel for schedule(dynamic, 64)
#endif
    for (int32_t order = start_idx; order < end_idx; order++) {
      size_t i = static_cast<size_t>(topology.sorted_indices[order]);
      const GridCell &cell = cells[i];

      if (!cell.is_channel) {
        route_overland_cell(params[i], states[i], cell, fast_flow[i],
                            slow_flow[i], step_seconds, cell.downstream_index,
                            states);
      } else {
        route_channel_cell(params[i], states[i], cell, fast_flow[i],
                           slow_flow[i], step_seconds, cell.downstream_index,
                           states);
      }
    }
  }

  // Compute final discharge (parallelizable)
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    const GridCell &cell = cells[i];

    if (!cell.is_channel) {
      float q = static_cast<float>(states[i].incoming_fastflow) * cell.hor_len;
      q += static_cast<float>(states[i].incoming_interflow) * cell.area / 3.6f;
      discharge[i] = q;
    } else {
      discharge[i] = static_cast<float>(states[i].incoming_fastflow);
    }
  }
}

void initialize_states(const Parameters *params, State *states,
                       size_t n_cells) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    states[i].prev_channel_q = 0.0f;
    states[i].prev_overland_q = 0.0f;
    states[i].interflow_reservoir = params[i].isu;
    states[i].incoming_water_overland = 0.0;
    states[i].incoming_water_channel = 0.0;
    states[i].incoming_interflow = 0.0;
    states[i].incoming_fastflow = 0.0;
  }
}

void reset_incoming_flows(State *states, size_t n_cells) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    states[i].incoming_water_overland = 0.0;
    states[i].incoming_water_channel = 0.0;
    states[i].incoming_interflow = 0.0;
    states[i].incoming_fastflow = 0.0;
  }
}

void validate_parameters(Parameters *params, size_t n_cells) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    Parameters &p = params[i];

    if (p.alpha < 0.0f)
      p.alpha = 1.0f;
    if (p.alpha0 < 0.0f)
      p.alpha0 = 1.0f;
    if (p.beta < 0.0f)
      p.beta = 0.6f;
    p.leak_i = std::clamp(p.leak_i, 0.0f, 1.0f);
    if (p.under < 0.0f)
      p.under = 0.1f;
  }
}

void classify_channel_cells(GridCell *cells, const Parameters *params,
                            size_t n_cells) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    cells[i].is_channel =
        (cells[i].fac > static_cast<int64_t>(params[i].threshold));
  }
}

// ============================================================================
// State Access Functions (for Python I/O)
// ============================================================================

void get_states_prev_channel_q(const State *states, size_t n_cells,
                               float *out) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    out[i] = states[i].prev_channel_q;
  }
}

void set_states_prev_channel_q(State *states, size_t n_cells, const float *in) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    states[i].prev_channel_q = in[i];
  }
}

void get_states_prev_overland_q(const State *states, size_t n_cells,
                                float *out) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    out[i] = states[i].prev_overland_q;
  }
}

void set_states_prev_overland_q(State *states, size_t n_cells,
                                const float *in) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    states[i].prev_overland_q = in[i];
  }
}

void get_states_interflow_reservoir(const State *states, size_t n_cells,
                                    float *out) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    out[i] = states[i].interflow_reservoir;
  }
}

void set_states_interflow_reservoir(State *states, size_t n_cells,
                                    const float *in) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    states[i].interflow_reservoir = in[i];
  }
}

void get_all_states(const State *states, size_t n_cells, float *out) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    out[i * 3 + 0] = states[i].prev_channel_q;
    out[i * 3 + 1] = states[i].prev_overland_q;
    out[i * 3 + 2] = states[i].interflow_reservoir;
  }
}

void set_all_states(State *states, size_t n_cells, const float *in) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    states[i].prev_channel_q = in[i * 3 + 0];
    states[i].prev_overland_q = in[i * 3 + 1];
    states[i].interflow_reservoir = in[i * 3 + 2];
  }
}

} // namespace kinematic

// Implementation of RoutingTopology::compute_from_grid
void RoutingTopology::compute_from_grid(const GridCell *cells, size_t n_cells) {
  // Allocate level array
  std::vector<int32_t> cell_levels(n_cells, -1);

  // Count incoming edges for each cell
  std::vector<int32_t> in_degree(n_cells, 0);
  for (size_t i = 0; i < n_cells; i++) {
    int64_t ds = cells[i].downstream_index;
    if (ds != INVALID_DOWNSTREAM && ds >= 0 &&
        static_cast<size_t>(ds) < n_cells) {
      in_degree[ds]++;
    }
  }

  // Initialize level 0 with headwater cells (no incoming)
  std::vector<size_t> current_level;
  for (size_t i = 0; i < n_cells; i++) {
    if (in_degree[i] == 0) {
      current_level.push_back(i);
      cell_levels[i] = 0;
    }
  }

  // BFS to assign levels
  int32_t level = 0;
  std::vector<size_t> next_level;

  while (!current_level.empty()) {
    level_starts.push_back(static_cast<int32_t>(sorted_indices.size()));

    for (size_t idx : current_level) {
      sorted_indices.push_back(static_cast<int32_t>(idx));

      int64_t ds = cells[idx].downstream_index;
      if (ds != INVALID_DOWNSTREAM && ds >= 0 &&
          static_cast<size_t>(ds) < n_cells) {
        in_degree[ds]--;
        if (in_degree[ds] == 0 && cell_levels[ds] == -1) {
          cell_levels[ds] = level + 1;
          next_level.push_back(static_cast<size_t>(ds));
        }
      }
    }

    current_level = std::move(next_level);
    next_level.clear();
    level++;
  }

  num_levels = level;
}

} // namespace ef5
