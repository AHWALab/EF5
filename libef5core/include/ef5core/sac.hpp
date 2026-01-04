/*
 * EF5 Core Library - SAC-SMA Water Balance Model
 *
 * Sacramento Soil Moisture Accounting model.
 * To be fully implemented - placeholder for build system.
 */

#ifndef EF5CORE_SAC_HPP
#define EF5CORE_SAC_HPP

#include "types.hpp"
#include <cstddef>

namespace ef5 {
namespace sac {

// SAC-SMA has many more parameters than CREST
struct Parameters {
  float uztwm; // Upper zone tension water max (mm)
  float uzfwm; // Upper zone free water max (mm)
  float lztwm; // Lower zone tension water max (mm)
  float lzfsm; // Lower zone free water supplemental max (mm)
  float lzfpm; // Lower zone free water primary max (mm)
  float uzk;   // Upper zone lateral drainage rate
  float lzsk;  // Lower zone supplemental drainage rate
  float lzpk;  // Lower zone primary drainage rate
  float pfree; // Percolation fraction going to free water
  float zperc; // Maximum percolation rate
  float rexp;  // Percolation equation exponent
  float pctim; // Impervious fraction of basin
  float adimp; // Additional impervious area
  float riva;  // Riparian vegetation area
  float side;  // Ratio of deep percolation
  float rserv; // Fraction of lower zone unavailable for transpiration

  Parameters();
};

struct State {
  float uztwc; // Upper zone tension water content
  float uzfwc; // Upper zone free water content
  float lztwc; // Lower zone tension water content
  float lzfsc; // Lower zone free water supplemental content
  float lzfpc; // Lower zone free water primary content
  float adimc; // Additional impervious area content

  State();
};

// Placeholder - full implementation to follow
void water_balance_cell(const Parameters &params, State &state, float precip,
                        float pet, float step_hours, float &fast_flow,
                        float &slow_flow);

void water_balance_grid(const Parameters *params, State *states,
                        const float *precip, const float *pet, size_t n_cells,
                        float step_hours, float *fast_flow, float *slow_flow,
                        float *soil_moisture);

} // namespace sac
} // namespace ef5

#endif // EF5CORE_SAC_HPP
