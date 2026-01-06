/*
 * EF5 Core Library - Kinematic Wave Routing Model
 *
 * Kinematic wave approximation for channel flow routing.
 * Uses Newton-Raphson iteration to solve the nonlinear kinematic wave equation.
 */

#ifndef EF5CORE_KINEMATIC_HPP
#define EF5CORE_KINEMATIC_HPP

#include "types.hpp"
#include <cstddef>

namespace ef5 {
namespace kinematic {

// ============================================================================
// Parameters
// ============================================================================

/**
 * @brief Kinematic wave routing parameters.
 */
struct Parameters {
  float alpha;     ///< Channel kinematic wave parameter (α)
  float alpha0;    ///< Overland kinematic wave parameter
  float beta;      ///< Channel cross-section exponent (typically 0.6)
  float threshold; ///< Flow accumulation threshold for channel cells
  float leak_i;    ///< Interflow leak rate (fraction per timestep, 0-1)
  float under;     ///< Undersurface flow speed factor
  float isu;       ///< Initial interflow storage (mm)

  Parameters()
      : alpha(1.0f), alpha0(1.0f), beta(0.6f), threshold(1000.0f),
        leak_i(0.05f), under(0.1f), isu(0.0f) {}
};

// Number of parameters
constexpr int NUM_PARAMS = 7;

// Parameter indices
enum ParamIndex {
  PARAM_ALPHA = 0,
  PARAM_ALPHA0,
  PARAM_BETA,
  PARAM_TH,
  PARAM_LEAKI,
  PARAM_UNDER,
  PARAM_ISU
};

// ============================================================================
// States
// ============================================================================

/**
 * @brief Kinematic wave state per grid cell.
 */
struct State {
  float prev_channel_q;      ///< Previous channel discharge (m³/s or m³/s/m)
  float prev_overland_q;     ///< Previous overland discharge
  float interflow_reservoir; ///< Interflow reservoir storage (mm)

  // Routing intermediate values
  double incoming_water_overland; ///< Incoming overland flow from upstream
  double incoming_water_channel;  ///< Incoming channel flow from upstream
  double incoming_interflow;      ///< Incoming interflow
  double incoming_fastflow;       ///< Incoming fast flow

  State()
      : prev_channel_q(0.0f), prev_overland_q(0.0f), interflow_reservoir(0.0f),
        incoming_water_overland(0.0), incoming_water_channel(0.0),
        incoming_interflow(0.0), incoming_fastflow(0.0) {}
};

// ============================================================================
// Core Computation Functions
// ============================================================================

/**
 * @brief Newton-Raphson solver for kinematic wave equation.
 *
 * Solves: (Δt/Δx)·Q + α·Q^β = RHS
 *
 * @param dt_over_dx  Timestep divided by cell length (seconds/meters)
 * @param alpha       Kinematic wave parameter α
 * @param beta        Cross-section exponent β
 * @param incoming_q  Incoming discharge from upstream
 * @param prev_q      Previous timestep discharge
 * @param lateral_in  Lateral inflow (from CREST excess)
 * @param step_seconds Timestep in seconds
 * @return Computed discharge
 */
float newton_raphson_solve(float dt_over_dx, float alpha, float beta,
                           float incoming_q, float prev_q, float lateral_in,
                           float step_seconds);

/**
 * @brief Route flow for a single overland cell.
 *
 * @param params         Routing parameters
 * @param state          Cell state (updated in-place)
 * @param cell           Grid cell properties
 * @param fast_flow      Fast flow input (m/s from CREST)
 * @param slow_flow      Slow flow input (m/s from CREST)
 * @param step_seconds   Timestep in seconds
 * @param downstream_idx Index of downstream cell (-1 if outlet)
 * @param all_states     Array of all states (for updating downstream)
 */
void route_overland_cell(const Parameters &params, State &state,
                         const GridCell &cell, float fast_flow, float slow_flow,
                         float step_seconds, int64_t downstream_idx,
                         State *all_states);

/**
 * @brief Route flow for a single channel cell.
 *
 * @param params         Routing parameters
 * @param state          Cell state (updated in-place)
 * @param cell           Grid cell properties
 * @param fast_flow      Fast flow input (m/s from CREST)
 * @param slow_flow      Slow flow input (m/s from CREST)
 * @param step_seconds   Timestep in seconds
 * @param downstream_idx Index of downstream cell (-1 if outlet)
 * @param all_states     Array of all states (for updating downstream)
 */
void route_channel_cell(const Parameters &params, State &state,
                        const GridCell &cell, float fast_flow, float slow_flow,
                        float step_seconds, int64_t downstream_idx,
                        State *all_states);

/**
 * @brief Route flow for entire grid (sequential, upstream to downstream).
 *
 * Cells must be processed in upstream-to-downstream order.
 * Uses sorted_indices from RoutingTopology.
 *
 * @param cells          Grid cell array
 * @param params         Parameters array (one per cell)
 * @param states         State array (updated in-place)
 * @param fast_flow      Fast flow input array
 * @param slow_flow      Slow flow input array
 * @param discharge      Output: discharge array (m³/s)
 * @param n_cells        Number of cells
 * @param step_hours     Timestep in hours
 * @param sorted_indices Cell indices in processing order (upstream first)
 */
void route_grid(const GridCell *cells, const Parameters *params, State *states,
                const float *fast_flow, const float *slow_flow,
                float *discharge, size_t n_cells, float step_hours,
                const int32_t *sorted_indices);

/**
 * @brief Route flow using level-based parallelization.
 *
 * Cells at the same level have no dependencies and can be processed in
 * parallel. Levels are processed sequentially from headwaters (level 0) to
 * outlet.
 *
 * @param cells          Grid cell array
 * @param params         Parameters array
 * @param states         State array
 * @param fast_flow      Fast flow input
 * @param slow_flow      Slow flow input
 * @param discharge      Output: discharge
 * @param n_cells        Number of cells
 * @param step_hours     Timestep in hours
 * @param topology       Routing topology with level information
 */
void route_grid_parallel(const GridCell *cells, const Parameters *params,
                         State *states, const float *fast_flow,
                         const float *slow_flow, float *discharge,
                         size_t n_cells, float step_hours,
                         const RoutingTopology &topology);

/**
 * @brief Initialize routing states.
 *
 * @param params   Parameters array
 * @param states   State array to initialize
 * @param n_cells  Number of cells
 */
void initialize_states(const Parameters *params, State *states, size_t n_cells);

/**
 * @brief Reset accumulated incoming flows (called after each timestep).
 *
 * @param states   State array
 * @param n_cells  Number of cells
 */
void reset_incoming_flows(State *states, size_t n_cells);

/**
 * @brief Validate and clamp parameters.
 *
 * @param params   Parameters array (modified in-place)
 * @param n_cells  Number of cells
 */
void validate_parameters(Parameters *params, size_t n_cells);

/**
 * @brief Determine which cells are channel cells based on FAC threshold.
 *
 * @param cells     Grid cells array (is_channel updated in-place)
 * @param params    Parameters array (for threshold)
 * @param n_cells   Number of cells
 */
void classify_channel_cells(GridCell *cells, const Parameters *params,
                            size_t n_cells);

// ============================================================================
// State Access Functions (for Python I/O)
// ============================================================================

// State indices matching original EF5
enum StateIndex {
  STATE_PREV_CHANNEL_Q = 0, // Previous channel discharge
  STATE_PREV_OVERLAND_Q,    // Previous overland discharge
  STATE_INTERFLOW_RES,      // Interflow reservoir
  STATE_QTY
};

/**
 * @brief Get previous channel discharge (for saving states).
 */
void get_states_prev_channel_q(const State *states, size_t n_cells, float *out);

/**
 * @brief Set previous channel discharge (for loading states).
 */
void set_states_prev_channel_q(State *states, size_t n_cells, const float *in);

/**
 * @brief Get previous overland discharge.
 */
void get_states_prev_overland_q(const State *states, size_t n_cells,
                                float *out);

/**
 * @brief Set previous overland discharge.
 */
void set_states_prev_overland_q(State *states, size_t n_cells, const float *in);

/**
 * @brief Get interflow reservoir storage.
 */
void get_states_interflow_reservoir(const State *states, size_t n_cells,
                                    float *out);

/**
 * @brief Set interflow reservoir storage.
 */
void set_states_interflow_reservoir(State *states, size_t n_cells,
                                    const float *in);

/**
 * @brief Get all states into a single 2D array (n_cells x 3).
 *
 * Column order: [prev_channel_q, prev_overland_q, interflow_reservoir]
 *
 * @param states    State array
 * @param n_cells   Number of cells
 * @param out       Output: flat array of size n_cells * 3 (row-major)
 */
void get_all_states(const State *states, size_t n_cells, float *out);

/**
 * @brief Set all states from a single 2D array (n_cells x 3).
 *
 * @param states    State array
 * @param n_cells   Number of cells
 * @param in        Input: flat array of size n_cells * 3 (row-major)
 */
void set_all_states(State *states, size_t n_cells, const float *in);

} // namespace kinematic
} // namespace ef5

#endif // EF5CORE_KINEMATIC_HPP
