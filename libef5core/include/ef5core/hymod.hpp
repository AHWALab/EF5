/*
 * EF5 Core Library - HyMOD Model
 *
 * Hybrid conceptual-distributed rainfall-runoff model.
 * To be fully implemented - placeholder for build system.
 */

#ifndef EF5CORE_HYMOD_HPP
#define EF5CORE_HYMOD_HPP

#include "types.hpp"
#include <cstddef>

namespace ef5 {
namespace hymod {

struct Parameters {
  float huz;         // Maximum height of upper zone storage (mm)
  float b;           // Distribution of heights for upper zone storage
  float alp;         // Fraction going to quick release stores
  float nq;          // Number of quick release stores
  float kq;          // Quick release rate (1/day)
  float ks;          // Slow release rate (1/day)
  float precip_mult; // Precipitation multiplier
  float xcuz;        // Initial upper zone storage fraction
  float xs;          // Initial slow store
  float xq;          // Initial quick store

  Parameters();
};

struct State {
  float xhuz; // Height of upper zone storage
  float xcuz; // Contents of upper zone storage
  float xs;   // Slow store content
  float *xq;  // Quick store contents (array, size = nq)
  int num_qf; // Number of quick flow stores

  float precip_excess;
  float discharge_qf;
  float discharge_sf;

  State();
  ~State();
};

// Placeholder - full implementation to follow
void water_balance_cell(const Parameters &params, State &state, float precip,
                        float pet, float step_hours, float &fast_flow,
                        float &slow_flow);

void water_balance_grid(const Parameters *params, State *states,
                        const float *precip, const float *pet, size_t n_cells,
                        float step_hours, float *fast_flow, float *slow_flow,
                        float *soil_moisture);

} // namespace hymod
} // namespace ef5

#endif // EF5CORE_HYMOD_HPP
