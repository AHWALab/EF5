/*
 * EF5 Core Library - SAC-SMA Implementation (Stub)
 */

#include "ef5core/sac.hpp"

namespace ef5 {
namespace sac {

Parameters::Parameters()
    : uztwm(50.0f), uzfwm(40.0f), lztwm(130.0f), lzfsm(25.0f), lzfpm(60.0f),
      uzk(0.3f), lzsk(0.05f), lzpk(0.01f), pfree(0.06f), zperc(250.0f),
      rexp(1.5f), pctim(0.01f), adimp(0.0f), riva(0.0f), side(0.0f),
      rserv(0.3f) {}

State::State()
    : uztwc(0.0f), uzfwc(0.0f), lztwc(0.0f), lzfsc(0.0f), lzfpc(0.0f),
      adimc(0.0f) {}

void water_balance_cell(const Parameters &params, State &state, float precip,
                        float pet, float step_hours, float &fast_flow,
                        float &slow_flow) {
  // TODO: Implement full SAC-SMA water balance
  // This is a stub - will be implemented by extracting from SAC.cpp
  fast_flow = 0.0f;
  slow_flow = 0.0f;
}

void water_balance_grid(const Parameters *params, State *states,
                        const float *precip, const float *pet, size_t n_cells,
                        float step_hours, float *fast_flow, float *slow_flow,
                        float *soil_moisture) {
  for (size_t i = 0; i < n_cells; i++) {
    water_balance_cell(params[i], states[i], precip[i], pet[i], step_hours,
                       fast_flow[i], slow_flow[i]);
    soil_moisture[i] = 0.0f;
  }
}

} // namespace sac
} // namespace ef5
