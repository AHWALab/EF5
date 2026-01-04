/*
 * EF5 Core Library - CREST Water Balance Model Implementation
 */

#include "ef5core/crest.hpp"
#include <algorithm>
#include <cmath>

#if _OPENMP
#include <omp.h>
#endif

namespace ef5 {
namespace crest {

void water_balance_cell(const Parameters &params, State &state,
                        float precip_rate, float pet_rate, float step_hours,
                        float &fast_flow, float &slow_flow) {
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
      sm = wm;
    }

    // Variable infiltration capacity curve
    if (sm < wm) {
      double Wmaxm = wm * (1.0 + b); // Maximum storage capacity
      double A = Wmaxm * (1.0 - std::pow(1.0 - sm / wm, 1.0 / (1.0 + b)));

      if (precip_soil + A >= Wmaxm) {
        // Soil is saturated: excess goes to runoff
        R = precip_soil - (wm - sm);
        if (R < 0.0)
          R = 0.0;
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
          infiltration = 0.0;
        }

        R = precip_soil - infiltration;
        if (R < 0.0)
          R = 0.0;
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
  state.soil_moisture = static_cast<float>(Wo);

  // Convert excess to flow rates (m/s equivalent for routing)
  // Division by (step_hours * 3600) converts mm to m/s rate
  float time_factor = step_hours * 3600.0f;
  fast_flow = state.excess_overland / time_factor;
  slow_flow = state.excess_interflow / time_factor;
}

void water_balance_grid(const Parameters *params, State *states,
                        const float *precip, const float *pet, size_t n_cells,
                        float step_hours, float *fast_flow, float *slow_flow,
                        float *soil_moisture) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    water_balance_cell(params[i], states[i], precip[i], pet[i], step_hours,
                       fast_flow[i], slow_flow[i]);

    // Output soil moisture as percentage of max capacity
    soil_moisture[i] = states[i].soil_moisture * 100.0f / params[i].wm;
  }
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

void validate_parameters(Parameters *params, size_t n_cells) {
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (size_t i = 0; i < n_cells; i++) {
    Parameters &p = params[i];

    // WM: max soil water capacity, must be positive
    if (p.wm < 1.0f)
      p.wm = 100.0f;

    // B exponent: must be non-negative
    if (p.b < 0.0f)
      p.b = 0.0f;
    if (!std::isfinite(p.b))
      p.b = 0.0f;

    // IM: impervious fraction, clamp to [0, 1]
    p.im = std::clamp(p.im, 0.0f, 1.0f);

    // KE: evaporation coefficient, should be positive
    if (p.ke < 0.0f)
      p.ke = 1.0f;

    // FC: field capacity, should be positive
    if (p.fc < 0.0f)
      p.fc = 1.0f;

    // IWU: initial water content %, clamp to [0, 100]
    p.iwu = std::clamp(p.iwu, 0.0f, 100.0f);

    // KSAT: saturated hydraulic conductivity, should be positive
    if (p.ksat < 0.0f)
      p.ksat = 45.0f;
  }
}

} // namespace crest
} // namespace ef5
