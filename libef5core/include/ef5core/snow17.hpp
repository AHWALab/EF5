/*
 * EF5 Core Library - Snow17 Snow Accumulation and Melt Model
 *
 * NWS Snow-17 temperature index model.
 * To be fully implemented - placeholder for build system.
 */

#ifndef EF5CORE_SNOW17_HPP
#define EF5CORE_SNOW17_HPP

#include "types.hpp"
#include <cstddef>

namespace ef5 {
namespace snow17 {

struct Parameters {
  float scf;    // Snow correction factor
  float mfmax;  // Maximum melt factor during non-rain periods (mm/°C/6hr)
  float mfmin;  // Minimum melt factor during non-rain periods
  float uadj;   // Wind function adjustment
  float si;     // Mean areal water equivalent above which 100% cover
  float pxtemp; // Temperature dividing rain from snow (°C)
  float nmf;    // Maximum negative melt factor
  float tipm;   // Antecedent temperature index parameter
  float mbase;  // Base temp for non-rain melt computations (°C)
  float plwhc;  // Percent liquid water holding capacity of snow
  float daygm;  // Daily ground melt (mm)
  float elev;   // Elevation (m) for lapse rate

  Parameters();
};

struct State {
  float ati;     // Antecedent temperature index
  float w_q;     // Liquid water in snow
  float w_i;     // Ice portion of snow
  float deficit; // Heat deficit

  State();
};

// Placeholder - full implementation to follow
void snow_balance_cell(const Parameters &params, State &state, float precip,
                       float temp, float jday, float step_hours, float &melt,
                       float &swe);

void snow_balance_grid(const Parameters *params, State *states,
                       const float *precip, const float *temp, size_t n_cells,
                       float jday, float step_hours, float *melt, float *swe);

} // namespace snow17
} // namespace ef5

#endif // EF5CORE_SNOW17_HPP
