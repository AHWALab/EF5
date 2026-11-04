// ef5 --check: find model cells with NaN or infinite parameters and the river
// channels their water cuts, without running the simulation.
//
// The models are set up exactly as for a run (same basin, gauges, parameter
// sets, parameter grids and th), and the parameters are read back from the
// models, so the check sees the values EF5 uses. Every cell downstream of a
// bad cell is marked: with ROUTING=KW its discharge becomes NaN as soon as the
// bad parameter is used (e.g. when it rains on that cell), and stays NaN.
//
// Writes check_channels.tif and check_breaks.csv to the task's OUTPUT folder.

#include "BasicGrids.h"
#include "Messages.h"
#include "Model.h"
#include "Simulator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{

// Cell classes in check_channels.tif (cells outside the basin are nodata)
enum CheckClass
{
  CHECK_HILLSLOPE = 1,
  CHECK_CHANNEL = 2,
  CHECK_CHANNEL_CUT = 3,
  CHECK_BAD_PARAMS = 4,
  CHECK_HILLSLOPE_CUT = 5,
};

// A channel cell where water from bad cells first enters a river
struct BreakPoint
{
  size_t node;
  bool cutAbove;            // the river is already cut upstream of here
  unsigned long wbMask;     // bad water balance parameters of the sources
  unsigned long routeMask;  // bad routing parameters of the sources
  size_t numSources;        // bad cells whose water enters here
  size_t source;            // the source with the largest FAC
  size_t channelCellsBelow; // channel cells from here to the outlet
};

bool BreakPointOrder(const BreakPoint &a, const BreakPoint &b)
{
  if (a.cutAbove != b.cutAbove)
  {
    return !a.cutAbove;
  }
  return a.channelCellsBelow > b.channelCellsBelow;
}

bool HasDownstream(const GridNode &node, size_t numNodes)
{
  return node.downStreamNode != INVALID_DOWNSTREAM_NODE &&
         node.downStreamNode < numNodes;
}

void CellCentre(const GridNode &node, float *lon, float *lat)
{
  float x = g_DEM->extent.left + (node.x + 0.5f) * g_DEM->cellSize;
  float y = g_DEM->extent.top - (node.y + 0.5f) * g_DEM->cellSize;
  g_Projection->UnprojectPoint(x, y, lon, lat);
}

std::string ParamList(unsigned long mask, const char *model,
                      const char **names, int count)
{
  std::string list;
  for (int p = 0; p < count; p++)
  {
    if (mask & (1ul << p))
    {
      if (!list.empty())
      {
        list += " ";
      }
      list += std::string(model) + "." + names[p];
    }
  }
  return list;
}

// The value of grid that the models multiply into node's parameter, or false
// if node is outside the grid (the models then use the scalar alone)
bool GridValue(FloatGrid *grid, const GridNode &node, float *value)
{
  GridLoc pt;
  if (g_DEM->IsSpatialMatch(grid))
  {
    *value = grid->data[node.y][node.x];
    return true;
  }
  if (grid->GetGridLoc(node.refLoc.x, node.refLoc.y, &pt))
  {
    *value = grid->data[pt.y][pt.x];
    return true;
  }
  return false;
}

// A parameter grid that holds its nodata value at modelled cells. The models
// multiply it in as a number (and may then silently reset the result), so the
// grid is not used there.
struct NodataGrid
{
  size_t param;
  size_t cells;        // modelled cells
  size_t channelCells; // of which channel cells
  bool everywhere;     // every cell of the grid is nodata: it was not read
};

void FindNodataGrids(std::vector<GridNode> *nodes,
                     const std::vector<bool> &channel,
                     std::vector<FloatGrid *> *grids,
                     std::vector<NodataGrid> *found)
{
  for (size_t p = 0; p < grids->size(); p++)
  {
    FloatGrid *grid = grids->at(p);
    if (!grid)
    {
      continue;
    }
    NodataGrid ng = {p, 0, 0, true};
    float value;
    for (size_t i = 0; i < nodes->size(); i++)
    {
      if (nodes->at(i).gauge && GridValue(grid, nodes->at(i), &value) &&
          value == grid->noData)
      {
        ng.cells++;
        if (channel[i])
        {
          ng.channelCells++;
        }
      }
    }
    if (!ng.cells)
    {
      continue;
    }
    for (long r = 0; ng.everywhere && r < grid->numRows; r++)
    {
      for (long c = 0; ng.everywhere && c < grid->numCols; c++)
      {
        ng.everywhere = (grid->data[r][c] == grid->noData);
      }
    }
    found->push_back(ng);
  }
}

bool IsChannelOnly(bool kinematic, size_t p)
{
  return kinematic &&
         (p == PARAM_KINEMATIC_ALPHA || p == PARAM_KINEMATIC_BETA);
}

// The --check listing of the nodata grids of one model
void ReportNodataGrids(std::vector<GridNode> *nodes,
                       const std::vector<bool> &channel,
                       std::vector<FloatGrid *> *grids,
                       std::vector<std::string> *files, const char *model,
                       const char **names, bool kinematic, bool *header)
{
  std::vector<NodataGrid> found;
  FindNodataGrids(nodes, channel, grids, &found);
  for (size_t g = 0; g < found.size(); g++)
  {
    const NodataGrid &ng = found[g];
    if (!*header)
    {
      printf("  Parameter grids holding nodata at modelled cells (EF5 uses "
             "the nodata value as a number):\n");
      *header = true;
    }
    printf("    %s.%-10s %zu cells", model, names[ng.param], ng.cells);
    if (IsChannelOnly(kinematic, ng.param))
    {
      printf(" (%zu on channel cells, where it is used)", ng.channelCells);
    }
    printf("\n");
    if (ng.everywhere)
    {
      printf("      WARNING: every cell of %s reads as nodata (%g); EF5 "
             "could not read this grid\n",
             files->at(ng.param).c_str(), grids->at(ng.param)->noData);
    }
  }
}

// Cells whose parameters, as the models use them, are NaN or infinite. alpha
// and beta of the kinematic wave only count on channel cells, where they are
// used. Call after the models' InitializeModel.
struct BadParams
{
  std::vector<unsigned long> wbMask, routeMask; // per cell: bit p = param p
  std::vector<size_t> wbCount, routeCount;      // cells per parameter
  std::vector<bool> channel, bad;               // per cell
  size_t numModelled, numChannel, numBad;
};

void FindBadParams(std::vector<GridNode> *nodes, WaterBalanceModel *wbModel,
                   int numWB, RoutingModel *rModel, int numRoute,
                   bool kinematic, BadParams *bp)
{
  size_t numNodes = nodes->size();
  bp->wbMask.assign(numNodes, 0);
  bp->routeMask.assign(numNodes, 0);
  bp->wbCount.assign(numWB, 0);
  bp->routeCount.assign(numRoute, 0);
  bp->channel.assign(numNodes, false);
  bp->bad.assign(numNodes, false);
  bp->numModelled = bp->numChannel = bp->numBad = 0;
  for (size_t i = 0; i < numNodes; i++)
  {
    if (!nodes->at(i).gauge)
    {
      continue;
    }
    bp->numModelled++;
    bp->channel[i] = rModel && nodes->at(i).channelGridCell;
    if (bp->channel[i])
    {
      bp->numChannel++;
    }
    const float *params = wbModel->GetCellParams(i);
    for (int p = 0; params && p < numWB; p++)
    {
      if (!std::isfinite(params[p]))
      {
        bp->wbMask[i] |= 1ul << p;
        bp->wbCount[p]++;
      }
    }
    params = rModel ? rModel->GetCellParams(i) : NULL;
    for (int p = 0; params && p < numRoute; p++)
    {
      if (!std::isfinite(params[p]) &&
          (bp->channel[i] || !IsChannelOnly(kinematic, p)))
      {
        bp->routeMask[i] |= 1ul << p;
        bp->routeCount[p]++;
      }
    }
    bp->bad[i] = bp->wbMask[i] || bp->routeMask[i];
    if (bp->bad[i])
    {
      bp->numBad++;
    }
  }
}

bool BuildPath(char *buffer, size_t size, const char *dir, const char *file)
{
  int len = snprintf(buffer, size, "%s/%s", dir, file);
  if (len < 0 || (size_t)len >= size)
  {
    ERROR_LOGF("Output path too long: %s/%s", dir, file);
    return false;
  }
  return true;
}

} // namespace

bool Simulator::CheckChannels(TaskConfigSection *taskN)
{
  task = taskN;
  zarrGridWriter = NULL;

  if (!InitializeBasic(task))
  {
    return false;
  }
  if (wbModel->IsLumped())
  {
    ERROR_LOGF("Task %s: the channel check needs a distributed model, %s is "
               "lumped",
               task->GetName(), wbModel->GetName());
    return false;
  }

  wbModel->InitializeModel(&nodes, &fullParamSettings, &paramGrids);
  if (rModel)
  {
    rModel->InitializeModel(&nodes, &fullParamSettingsRoute, &paramGridsRoute);
  }

  int model = task->GetModel();
  int routing = task->GetRouting();
  int numWB = numModelParams[model];
  int numRoute = rModel ? numRouteParams[routing] : 0;
  const char **wbNames = modelParamStrings[model];
  const char **routeNames = rModel ? routeParamStrings[routing] : NULL;
  const char *routeName = rModel ? routeStrings[routing] : "";
  bool kinematic = (routing == ROUTE_KINEMATIC ||
                    routing == ROUTE_KINEMATIC_PARALLEL);
  size_t numNodes = nodes.size();

  // 1. Bad cells
  BadParams bp;
  FindBadParams(&nodes, wbModel, numWB, rModel, numRoute, kinematic, &bp);
  const std::vector<unsigned long> &wbMask = bp.wbMask, &routeMask = bp.routeMask;
  const std::vector<size_t> &wbCount = bp.wbCount, &routeCount = bp.routeCount;
  const std::vector<bool> &channel = bp.channel, &bad = bp.bad;
  size_t numModelled = bp.numModelled, numChannel = bp.numChannel,
         numBad = bp.numBad;

  // 2. Everything downstream of a bad cell is cut. A walk stops at the first
  // cell already marked, since everything below it is marked too.
  std::vector<bool> cut(numNodes, false);
  for (size_t i = 0; i < numNodes; i++)
  {
    if (!bad[i])
    {
      continue;
    }
    size_t j = i;
    while (!cut[j])
    {
      cut[j] = true;
      if (!HasDownstream(nodes[j], numNodes))
      {
        break;
      }
      j = nodes[j].downStreamNode;
    }
  }

  // Channel cells with a cut channel cell directly upstream
  std::vector<bool> cutAbove(numNodes, false);
  for (size_t i = 0; i < numNodes; i++)
  {
    if (channel[i] && cut[i] && HasDownstream(nodes[i], numNodes))
    {
      cutAbove[nodes[i].downStreamNode] = true;
    }
  }

  // 3. Break points: the first channel cell each bad cell's water reaches
  std::vector<long> breakIndex(numNodes, -1);
  std::vector<BreakPoint> breaks;
  for (size_t i = 0; i < numNodes; i++)
  {
    if (!bad[i])
    {
      continue;
    }
    size_t j = i;
    while (!channel[j] && HasDownstream(nodes[j], numNodes))
    {
      j = nodes[j].downStreamNode;
    }
    if (!channel[j])
    {
      continue; // reaches the outlet without entering a channel
    }
    if (breakIndex[j] < 0)
    {
      breakIndex[j] = breaks.size();
      BreakPoint bp = {j, cutAbove[j], 0, 0, 0, i, 0};
      for (size_t k = j;; k = nodes[k].downStreamNode)
      {
        if (channel[k])
        {
          bp.channelCellsBelow++;
        }
        if (!HasDownstream(nodes[k], numNodes))
        {
          break;
        }
      }
      breaks.push_back(bp);
    }
    BreakPoint *bp = &breaks[breakIndex[j]];
    bp->wbMask |= wbMask[i];
    bp->routeMask |= routeMask[i];
    bp->numSources++;
    if (nodes[i].fac > nodes[bp->source].fac)
    {
      bp->source = i;
    }
  }
  std::sort(breaks.begin(), breaks.end(), BreakPointOrder);

  size_t numChannelCut = 0, numNewBreaks = 0;
  std::vector<float> classes(numNodes, 0.0f);
  for (size_t i = 0; i < numNodes; i++)
  {
    if (bad[i])
    {
      classes[i] = CHECK_BAD_PARAMS;
    }
    else if (cut[i])
    {
      classes[i] = channel[i] ? CHECK_CHANNEL_CUT : CHECK_HILLSLOPE_CUT;
    }
    else
    {
      classes[i] = channel[i] ? CHECK_CHANNEL : CHECK_HILLSLOPE;
    }
    if (channel[i] && cut[i])
    {
      numChannelCut++;
    }
  }
  for (size_t b = 0; b < breaks.size(); b++)
  {
    if (!breaks[b].cutAbove)
    {
      numNewBreaks++;
    }
  }

  // 4. Outputs
  char path[CONFIG_MAX_LEN * 2];
  if (!BuildPath(path, sizeof(path), task->GetOutput(), "check_channels.tif"))
  {
    return false;
  }
  gridWriter.Initialize();
  gridWriter.WriteGrid(&nodes, &classes, path, false);
  std::string tifPath = path;

  if (!BuildPath(path, sizeof(path), task->GetOutput(), "check_breaks.csv"))
  {
    return false;
  }
  FILE *csv = fopen(path, "w");
  if (!csv)
  {
    ERROR_LOGF("Failed to open %s for writing", path);
    return false;
  }
  fprintf(csv, "lat,lon,fac,river_cut_above,bad_parameters,source_cells,"
               "source_lat,source_lon,channel_cells_below,gauge_below\n");
  for (size_t b = 0; b < breaks.size(); b++)
  {
    const BreakPoint &bp = breaks[b];
    float lon, lat, srcLon, srcLat;
    CellCentre(nodes[bp.node], &lon, &lat);
    CellCentre(nodes[bp.source], &srcLon, &srcLat);
    std::string params = ParamList(bp.wbMask, wbModel->GetName(), wbNames,
                                   numWB);
    std::string routeParams =
        ParamList(bp.routeMask, routeName, routeNames, numRoute);
    if (!params.empty() && !routeParams.empty())
    {
      params += " ";
    }
    params += routeParams;
    fprintf(csv, "%.6f,%.6f,%ld,%s,%s,%zu,%.6f,%.6f,%zu,%s\n", lat, lon,
            nodes[bp.node].fac, bp.cutAbove ? "yes" : "no", params.c_str(),
            bp.numSources, srcLat, srcLon, bp.channelCellsBelow,
            nodes[bp.node].gauge->GetName());
  }
  fclose(csv);

  // 5. Summary
  printf("\nChannel check for task %s (%s%s%s)\n", task->GetName(),
         wbModel->GetName(), rModel ? " + " : "", routeName);
  printf("  Modelled cells:        %zu, of which %zu channel (FAC > th)\n",
         numModelled, numChannel);
  printf("  Cells with NaN or infinite parameters: %zu\n", numBad);
  for (int p = 0; p < numWB; p++)
  {
    if (wbCount[p])
    {
      printf("    %s.%-10s %zu cells\n", wbModel->GetName(), wbNames[p],
             wbCount[p]);
    }
  }
  for (int p = 0; p < numRoute; p++)
  {
    if (routeCount[p])
    {
      printf("    %s.%-10s %zu cells\n", routeName, routeNames[p],
             routeCount[p]);
    }
  }
  bool header = false;
  ReportNodataGrids(&nodes, channel, &paramGrids,
                    task->GetParamsSec()->GetParamGrids(), wbModel->GetName(),
                    wbNames, false, &header);
  if (rModel)
  {
    ReportNodataGrids(&nodes, channel, &paramGridsRoute,
                      task->GetRoutingParamsSec()->GetParamGrids(), routeName,
                      routeNames, kinematic, &header);
  }
  if (!rModel)
  {
    printf("  No routing: channels not checked\n");
  }
  else
  {
    printf("  Channel cells cut:     %zu of %zu\n", numChannelCut, numChannel);
    printf("  Break points:          %zu (%zu where a good river is cut, %zu "
           "on a river already cut above)\n",
           breaks.size(), numNewBreaks, breaks.size() - numNewBreaks);
    if (routing == ROUTE_KINEMATIC)
    {
      printf("  With ROUTING=KW the discharge of a cut cell becomes NaN once a "
             "bad parameter is used\n  (e.g. when it rains on the bad cell), "
             "and stays NaN for the rest of the run.\n");
    }
    else if (routing == ROUTE_KINEMATIC_PARALLEL)
    {
      printf("  ROUTING=KW_PARALLEL sets non-finite flow to 0, so the cut "
             "cells miss the bad cells' water\n  instead of showing NaN.\n");
    }

    size_t numGauges = 0;
    std::string cutGauges;
    for (size_t g = 0; g < gauges->size(); g++)
    {
      GaugeConfigSection *gauge = gauges->at(g);
      long idx = gauge->GetGridNodeIndex();
      if (idx < 0 || (size_t)idx >= numNodes || nodes[idx].gauge != gauge)
      {
        continue;
      }
      if (cut[idx])
      {
        numGauges++;
        if (numGauges <= 20)
        {
          cutGauges += std::string(numGauges > 1 ? ", " : "") +
                       gauge->GetName();
        }
      }
    }
    printf("  Gauges below a cut:    %zu%s%s%s\n", numGauges,
           numGauges ? " (" : "", cutGauges.c_str(),
           numGauges > 20 ? ", ...)" : (numGauges ? ")" : ""));
  }
  printf("  Wrote %s\n", tifPath.c_str());
  printf("        classes: 1 hillslope, 2 channel, 3 channel cut, 4 bad "
         "parameters, 5 hillslope cut\n");
  printf("  Wrote %s (%zu break points)\n\n", path, breaks.size());

  return true;
}

// Start-up warnings for a run (called once the models are initialized):
// parameters that are NaN or infinite, and parameter grids that hold their
// nodata value at modelled cells. ef5 --check gives the locations.
void Simulator::WarnParameterProblems()
{
  int model = task->GetModel();
  int routing = task->GetRouting();
  int numWB = numModelParams[model];
  int numRoute = rModel ? numRouteParams[routing] : 0;
  const char **wbNames = modelParamStrings[model];
  const char **routeNames = rModel ? routeParamStrings[routing] : NULL;
  const char *routeName = rModel ? routeStrings[routing] : "";
  bool kinematic = (routing == ROUTE_KINEMATIC ||
                    routing == ROUTE_KINEMATIC_PARALLEL);

  BadParams bp;
  FindBadParams(&nodes, wbModel, numWB, rModel, numRoute, kinematic, &bp);
  for (int p = 0; p < numWB + numRoute; p++)
  {
    bool wb = p < numWB;
    size_t count = wb ? bp.wbCount[p] : bp.routeCount[p - numWB];
    if (count)
    {
      WARNING_LOGF("Parameter %s %s is NaN or infinite at %zu of %zu modelled "
                   "cells; discharge downstream of them can become NaN. Run "
                   "ef5 --check for the locations.",
                   wb ? wbModel->GetName() : routeName,
                   wb ? wbNames[p] : routeNames[p - numWB], count,
                   bp.numModelled);
    }
  }

  for (int r = 0; r < (rModel ? 2 : 1); r++)
  {
    std::vector<FloatGrid *> *grids = r ? &paramGridsRoute : &paramGrids;
    std::vector<std::string> *files =
        r ? task->GetRoutingParamsSec()->GetParamGrids()
          : task->GetParamsSec()->GetParamGrids();
    std::vector<NodataGrid> found;
    FindNodataGrids(&nodes, bp.channel, grids, &found);
    for (size_t g = 0; g < found.size(); g++)
    {
      const NodataGrid &ng = found[g];
      bool channelOnly = r && IsChannelOnly(kinematic, ng.param);
      if (channelOnly && !ng.channelCells)
      {
        continue; // only on hillslope cells, where it is not used
      }
      WARNING_LOGF("%s %s_grid (%s) holds its nodata value (%g) at %zu of %zu "
                   "modelled %s; EF5 uses it there as a number%s",
                   r ? routeName : wbModel->GetName(),
                   r ? routeNames[ng.param] : wbNames[ng.param],
                   files->at(ng.param).c_str(), grids->at(ng.param)->noData,
                   channelOnly ? ng.channelCells : ng.cells,
                   channelOnly ? bp.numChannel : bp.numModelled,
                   channelOnly ? "channel cells" : "cells",
                   ng.everywhere ? ". Every cell of the grid is nodata: it "
                                   "was not read"
                                 : "");
    }
  }
}
