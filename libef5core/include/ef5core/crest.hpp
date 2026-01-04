/*
 * EF5 Core Library - CREST Water Balance Model
 *
 * The Coupled Routing and Excess STorage (CREST) distributed hydrological
 * model.
 *
 * Citation: Wang et al. (2011) "The Coupled Routing and Excess Storage (CREST)
 * distributed hydrological model"
 */

#ifndef EF5CORE_CREST_HPP
#define EF5CORE_CREST_HPP

#include "types.hpp"
#include <cstddef>

namespace ef5 {
namespace crest {

// ============================================================================
// Parameters
// ============================================================================

/**
 * @brief CREST model parameters.
 *
 * These can be uniform across the basin or spatially varying (one per cell).
 */
struct Parameters {
  float wm;   ///< Maximum soil water capacity (mm), typically 50-500
  float b;    ///< B exponent for variable infiltration curve, typically 0.1-2.0
  float im;   ///< Impervious area fraction, 0-1
  float ke;   ///< Evaporation coefficient, typically 0.5-1.5
  float fc;   ///< Field capacity infiltration rate (mm/hr), typically 0.1-200
  float iwu;  ///< Initial water content as % of WM, 0-100 (for initialization)
  float ksat; ///< Saturated hydraulic conductivity (mm/hr), typically 1-100

  // Default constructor with reasonable defaults
  Parameters()
      : wm(150.0f), b(0.4f), im(0.05f), ke(1.0f), fc(10.0f), iwu(50.0f),
        ksat(45.0f) {}
};

// Number of parameters
constexpr int NUM_PARAMS = 7;

// Parameter indices (for flat array access)
enum ParamIndex {
  PARAM_WM = 0,
  PARAM_B,
  PARAM_IM,
  PARAM_KE,
  PARAM_FC,
  PARAM_IWU,
  PARAM_KSAT
};

// ============================================================================
// States
// ============================================================================

/**
 * @brief CREST model state per grid cell.
 */
struct State {
  float soil_moisture;    ///< Current soil moisture (mm)
  float excess_overland;  ///< Excess overland (mm) - intermediate output
  float excess_interflow; ///< Excess interflow (mm) - intermediate output
  float actual_et;        ///< Actual ET this timestep (mm)

  State()
      : soil_moisture(0.0f), excess_overland(0.0f), excess_interflow(0.0f),
        actual_et(0.0f) {}
};

// ============================================================================
// Core Computation Functions
// ============================================================================

/**
 * @brief Compute water balance for a single grid cell.
 *
 * This is the core computational kernel. It computes the CREST water balance
 * equations for one cell and one timestep.
 *
 * @param params     Model parameters for this cell
 * @param state      Cell state (soil_moisture is updated in-place)
 * @param precip     Precipitation input (mm/hr)
 * @param pet        Potential evapotranspiration input (mm/hr)
 * @param step_hours Timestep duration in hours
 * @param fast_flow  Output: fast flow / overland excess (m/s equivalent for
 * routing)
 * @param slow_flow  Output: slow flow / interflow excess (m/s equivalent for
 * routing)
 */
void water_balance_cell(const Parameters &params, State &state, float precip,
                        float pet, float step_hours, float &fast_flow,
                        float &slow_flow);

/**
 * @brief Compute water balance for entire grid (OpenMP parallelized).
 *
 * This processes all cells in one vectorized loop. The params, states, etc.
 * are all arrays with one element per cell.
 *
 * @param params       Parameter array, one per cell (or broadcast single
 * params)
 * @param states       State array, updated in-place
 * @param precip       Precipitation array (mm/hr)
 * @param pet          PET array (mm/hr)
 * @param n_cells      Number of cells
 * @param step_hours   Timestep in hours
 * @param fast_flow    Output: fast flow array (allocated by caller)
 * @param slow_flow    Output: slow flow array (allocated by caller)
 * @param soil_moisture Output: soil moisture % array (allocated by caller)
 */
void water_balance_grid(const Parameters *params, State *states,
                        const float *precip, const float *pet, size_t n_cells,
                        float step_hours, float *fast_flow, float *slow_flow,
                        float *soil_moisture);

/**
 * @brief Initialize states from initial water content fraction.
 *
 * @param params   Parameter array
 * @param states   State array to initialize
 * @param n_cells  Number of cells
 */
void initialize_states(const Parameters *params, State *states, size_t n_cells);

/**
 * @brief Validate and clamp parameters to valid ranges.
 *
 * @param params   Parameter array to validate (modified in-place)
 * @param n_cells  Number of cells
 */
void validate_parameters(Parameters *params, size_t n_cells);

} // namespace crest
} // namespace ef5

#endif // EF5CORE_CREST_HPP
