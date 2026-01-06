/*
 * EF5 Core Library - CREST Water Balance Model Implementation
 */

#include "ef5core/crest.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>

#if _OPENMP
#include <omp.h>
#endif

namespace ef5 {
namespace crest {

void water_balance_cell(const Parameters &params, State &state,
                        float precip_rate, float pet_rate, float step_hours,
                        float &fast_flow, float &slow_flow, Diagnostics *diag) {
  // Convert rates to totals for this timestep
  double precip = precip_rate * step_hours; // mm/hr -> mm
  double pet = pet_rate * step_hours;       // mm/hr -> mm
  double R = 0.0, Wo = 0.0;

  // Adjust PET by evaporation coefficient
  double adj_pet = pet * params.ke;
  double temX = 0.0;

  // Get current soil moisture
  double sm = state.soil_moisture;
  const double wm = params.wm;
  const double b = params.b;
  const double im = params.im;
  const double fc = params.fc;

  // Check for NaN in inputs
  if (diag &&
      (!std::isfinite(sm) || !std::isfinite(precip) || !std::isfinite(pet))) {
    diag->nan_values_fixed++;
    if (!std::isfinite(sm))
      sm = 0.0;
    if (!std::isfinite(precip))
      precip = 0.0;
    if (!std::isfinite(pet))
      pet = 0.0;
  }

  // Case 1: Precipitation exceeds adjusted PET (wet conditions)
  if (precip > adj_pet) {
    // Partition precipitation between pervious and impervious areas
    double precip_soil =
        (precip - adj_pet) * (1.0 - im); // Precip reaching soil
    double precip_imperv =
        precip - adj_pet - precip_soil; // Runoff from impervious

    // Handle excess interflow from previous overflow
    double interflow_excess = sm - wm;
    if (interflow_excess < 0.0) {
      interflow_excess = 0.0;
    }

    // Cap soil moisture at maximum
    if (sm > wm) {
      if (diag)
        diag->sm_clamped_high++;
      sm = wm;
    }

    // Variable infiltration capacity curve
    if (sm < wm) {
      double Wmaxm = wm * (1.0 + b); // Maximum storage capacity
      double A = Wmaxm * (1.0 - std::pow(1.0 - sm / wm, 1.0 / (1.0 + b)));

      if (precip_soil + A >= Wmaxm) {
        // Soil is saturated: excess goes to runoff
        R = precip_soil - (wm - sm);
        if (R < 0.0) {
          if (diag)
            diag->runoff_clamped++;
          R = 0.0;
        }
        Wo = wm;
      } else {
        // Partial infiltration using variable infiltration curve
        double infiltration =
            wm * (std::pow(1.0 - A / Wmaxm, 1.0 + b) -
                  std::pow(1.0 - (A + precip_soil) / Wmaxm, 1.0 + b));

        // Clamp infiltration
        if (infiltration > precip_soil) {
          infiltration = precip_soil;
        } else if (infiltration < 0.0) {
          if (diag)
            diag->infiltration_clamped++;
          infiltration = 0.0;
        }

        R = precip_soil - infiltration;
        if (R < 0.0) {
          if (diag)
            diag->runoff_clamped++;
          R = 0.0;
        }
        Wo = sm + infiltration;
      }
    } else {
      // Soil already at capacity
      R = precip_soil;
      Wo = wm;
    }

    // Split excess (R) between overland and interflow
    // temX = potential interflow based on field capacity
    temX = (sm + Wo) / wm / 2.0 * (fc * step_hours);

    double excess_interflow = (R <= temX) ? R : temX;
    double excess_overland = R - excess_interflow + precip_imperv;

    state.excess_overland = static_cast<float>(excess_overland);
    state.excess_interflow =
        static_cast<float>(excess_interflow + interflow_excess);
    state.actual_et = static_cast<float>(adj_pet);

  } else {
    // Case 2: PET exceeds precipitation (dry conditions)
    state.excess_overland = 0.0f;

    // Handle excess from overflow
    double interflow_excess = sm - wm;
    if (interflow_excess < 0.0) {
      interflow_excess = 0.0;
    }
    state.excess_interflow = static_cast<float>(interflow_excess);

    if (sm > wm) {
      if (diag)
        diag->sm_clamped_high++;
      sm = wm;
    }

    // Evaporate from soil storage
    double excess_et = (adj_pet - precip) * sm / wm;
    if (excess_et < sm) {
      Wo = sm - excess_et;
    } else {
      Wo = 0.0;
      excess_et = sm;
    }
    state.actual_et = static_cast<float>(excess_et + precip);
  }

  // Update soil moisture state
  if (Wo < 0.0) {
    if (diag)
      diag->sm_clamped_low++;
    Wo = 0.0;
  }
  state.soil_moisture = static_cast<float>(Wo);

  // Convert excess to flow rates (m/s equivalent for routing)
  // Division by (step_hours * 3600) converts mm to m/s rate
  float time_factor = step_hours * 3600.0f;
  fast_flow = state.excess_overland / time_factor;
  slow_flow = state.excess_interflow / time_factor;
}

Diagnostics water_balance_grid(const Parameters *params, State *states,
                               const float *precip, const float *pet,
                               size_t n_cells, float step_hours,
                               float *fast_flow, float *slow_flow,
                               float *soil_moisture) {
  Diagnostics total_diag;
  total_diag.cells_processed = n_cells;

  // Thread-local diagnostics for parallel reduction
#if _OPENMP
#pragma omp parallel
  {
    Diagnostics local_diag;

#pragma omp for schedule(static)
    for (size_t i = 0; i < n_cells; i++) {
      water_balance_cell(params[i], states[i], precip[i], pet[i], step_hours,
                         fast_flow[i], slow_flow[i], &local_diag);

      // Output soil moisture as percentage of max capacity
      soil_moisture[i] = states[i].soil_moisture * 100.0f / params[i].wm;
    }

// Reduce diagnostics (critical section)
#pragma omp critical
    {
      total_diag.sm_clamped_low += local_diag.sm_clamped_low;
      total_diag.sm_clamped_high += local_diag.sm_clamped_high;
      total_diag.runoff_clamped += local_diag.runoff_clamped;
      total_diag.infiltration_clamped += local_diag.infiltration_clamped;
      total_diag.nan_values_fixed += local_diag.nan_values_fixed;
    }
  }
#else
  Diagnostics local_diag;
  for (size_t i = 0; i < n_cells; i++) {
    water_balance_cell(params[i], states[i], precip[i], pet[i], step_hours,
                       fast_flow[i], slow_flow[i], &local_diag);
    soil_moisture[i] = states[i].soil_moisture * 100.0f / params[i].wm;
  }
  total_diag = local_diag;
#endif

  return total_diag;
}

void initialize_states(const Parameters *params, State *states,
                       size_t n_cells) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    // Initialize soil moisture from IWU parameter (% of WM)
    states[i].soil_moisture = params[i].iwu * params[i].wm / 100.0f;
    states[i].excess_overland = 0.0f;
    states[i].excess_interflow = 0.0f;
    states[i].actual_et = 0.0f;
  }
}

Diagnostics validate_parameters(Parameters *params, size_t n_cells) {
  Diagnostics diag;
  diag.cells_processed = n_cells;

#if _OPENMP
#pragma omp parallel
  {
    size_t local_count = 0;

#pragma omp for schedule(static)
    for (size_t i = 0; i < n_cells; i++) {
      Parameters &p = params[i];
      bool clamped = false;

      // WM: max soil water capacity, must be positive
      if (p.wm < 1.0f) {
        p.wm = 100.0f;
        clamped = true;
      }

      // B exponent: must be non-negative
      if (p.b < 0.0f) {
        p.b = 0.0f;
        clamped = true;
      }
      if (!std::isfinite(p.b)) {
        p.b = 0.0f;
        clamped = true;
      }

      // IM: impervious fraction, clamp to [0, 1]
      if (p.im < 0.0f || p.im > 1.0f) {
        p.im = std::clamp(p.im, 0.0f, 1.0f);
        clamped = true;
      }

      // KE: evaporation coefficient, should be positive
      if (p.ke < 0.0f) {
        p.ke = 1.0f;
        clamped = true;
      }

      // FC: field capacity, should be positive
      if (p.fc < 0.0f) {
        p.fc = 1.0f;
        clamped = true;
      }

      // IWU: initial water content %, clamp to [0, 100]
      if (p.iwu < 0.0f || p.iwu > 100.0f) {
        p.iwu = std::clamp(p.iwu, 0.0f, 100.0f);
        clamped = true;
      }

      // KSAT: saturated hydraulic conductivity, should be positive
      if (p.ksat < 0.0f) {
        p.ksat = 45.0f;
        clamped = true;
      }

      if (clamped)
        local_count++;
    }

#pragma omp atomic
    diag.sm_clamped_low +=
        local_count; // Reusing this field for param clamp count
  }
#else
  for (size_t i = 0; i < n_cells; i++) {
    Parameters &p = params[i];
    bool clamped = false;

    if (p.wm < 1.0f) {
      p.wm = 100.0f;
      clamped = true;
    }
    if (p.b < 0.0f) {
      p.b = 0.0f;
      clamped = true;
    }
    if (!std::isfinite(p.b)) {
      p.b = 0.0f;
      clamped = true;
    }
    if (p.im < 0.0f || p.im > 1.0f) {
      p.im = std::clamp(p.im, 0.0f, 1.0f);
      clamped = true;
    }
    if (p.ke < 0.0f) {
      p.ke = 1.0f;
      clamped = true;
    }
    if (p.fc < 0.0f) {
      p.fc = 1.0f;
      clamped = true;
    }
    if (p.iwu < 0.0f || p.iwu > 100.0f) {
      p.iwu = std::clamp(p.iwu, 0.0f, 100.0f);
      clamped = true;
    }
    if (p.ksat < 0.0f) {
      p.ksat = 45.0f;
      clamped = true;
    }

    if (clamped)
      diag.sm_clamped_low++;
  }
#endif

  return diag;
}

// ============================================================================
// State Access Functions (for Python I/O)
// ============================================================================

void get_states_soil_moisture(const State *states, size_t n_cells,
                              float *out_sm) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    out_sm[i] = states[i].soil_moisture;
  }
}

void set_states_soil_moisture(State *states, size_t n_cells,
                              const float *in_sm) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    states[i].soil_moisture = in_sm[i];
  }
}

void get_actual_et(const State *states, size_t n_cells, float *out_et) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    out_et[i] = states[i].actual_et;
  }
}

} // namespace crest
} // namespace ef5
