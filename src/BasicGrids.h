#ifndef BASIC_GRIDS_H
#define BASIC_GRIDS_H

#include "BasinConfigSection.h"
#include "GaugeConfigSection.h"
#include "GaugeMap.h"
#include "Grid.h"
#include "GridNode.h"
#include "Projection.h"
#include <vector>

bool LoadBasicGrids();
void FreeBasicGridsData();
void ClipBasicGrids(long x, long y, long search, const char *output);
void ClipBasicGrids(BasinConfigSection *basin, std::vector<GridNode> *nodes,
                    const char *name, const char *output);
// The parameter sets of one kind of model (water balance, routing, snow or
// inundation), for CarveBasin.
struct BasinParamSets
{
  // The sets given for gauges in the control file. NULL if the task does not
  // use this kind of model.
  std::map<GaugeConfigSection *, float *> *in;
  // Filled by CarveBasin: the set each gauge in the basin uses (its own, or
  // the one of the gauge downstream).
  std::map<GaugeConfigSection *, float *> *out;
  // The set for basin outlet gauges without their own. May be NULL.
  float *defaults;

  BasinParamSets(std::map<GaugeConfigSection *, float *> *in = NULL,
                 std::map<GaugeConfigSection *, float *> *out = NULL,
                 float *defaults = NULL)
      : in(in), out(out), defaults(defaults) {}
};

// Finds the cells of a basin: every cell upstream of its outlet gauges, in walk
// order (downstream before upstream), with slopes, areas and the gauge each
// cell drains to. Also links the gauges in gaugeMap and gives each gauge its
// parameter sets.
void CarveBasin(BasinConfigSection *basin, std::vector<GridNode> *nodes,
                GaugeMap *gaugeMap, const BasinParamSets &waterBalance,
                const BasinParamSets &routing,
                const BasinParamSets &snow = BasinParamSets(),
                const BasinParamSets &inundation = BasinParamSets());
void MakeBasic();
void ReclassifyDDM();
bool CheckESRIDDM();
bool CheckSimpleDDM();
void FindIndBasins(float left, float right, float top, float bottom, const char *output);

extern FloatGrid *g_DEM;
extern FloatGrid *g_DDM;
extern FloatGrid *g_FAM;
extern Projection *g_Projection;

#endif
