#ifndef KW_WAVEFRONT_MODEL_H
#define KW_WAVEFRONT_MODEL_H

#include "ModelBase.h"
#include <vector>

// Same layer / state layout as serial KW (KinematicRoute.h).
// Separate enums keep this router independent of KWRoute / KWRouteParallel.

enum KW_WAVEFRONT_LAYER
{
  KW_WF_LAYER_FASTFLOW,
  KW_WF_LAYER_INTERFLOW,
  KW_WF_LAYER_QTY,
};

enum STATES_KW_WAVEFRONT
{
  STATE_KW_WF_PQ,
  STATE_KW_WF_PO,
  STATE_KW_WF_IR,
  STATE_KW_WF_QTY
};

struct KWGridNodeWavefront : BasicGridNode
{
  float params[PARAM_KINEMATIC_QTY];
  float states[STATE_KW_WF_QTY];

  bool channelGridCell;
  bool daActive;
  double slopeSqrt;
  double hillSlopeSqrt;

  double nexTime[KW_WF_LAYER_QTY];
  KWGridNodeWavefront *routeCNode[2][KW_WF_LAYER_QTY];
  GridNode *routeNode[2][KW_WF_LAYER_QTY];
  double routeAmount[2][KW_WF_LAYER_QTY];
  double incomingWater[KW_WF_LAYER_QTY];

  double incomingWaterOverland, incomingWaterChannel;

  // Hot-path caches (filled in InitializeParameters / InitializeRouting).
  // This is to avoid repeating divisions and param multiplies inside Newton loops.
  float invHorLen;      // 1 / node->horLen
  float alpha0;         // PARAM_KINEMATIC_ALPHA0
  float alpha0Beta;     // alpha0 * 0.6 (overland)
  float alphaCh;        // PARAM_KINEMATIC_ALPHA (channel)
  float betaCh;         // PARAM_KINEMATIC_BETA (channel)
  float alphaBetaCh;    // alphaCh * betaCh
  long downStreamIndex; // cached modelIndex, or INVALID_DOWNSTREAM_NODE
};

// Wavefront OpenMP kinematic router:
// - Same Newton physics as KWRoute (serial)
// - Cells grouped into topological levels from downStreamNode
// - Parallel only within a level; sync between levels
class KWRouteWavefront : public RoutingModel
{

public:
  KWRouteWavefront();
  ~KWRouteWavefront();
  float SetObsInflow(long index, float inflow);
  bool InitializeModel(std::vector<GridNode> *newNodes,
                       std::map<GaugeConfigSection *, float *> *paramSettings,
                       std::vector<FloatGrid *> *paramGrids);
  void InitializeStates(TimeVar *beginTime, char *statePath,
                        std::vector<float> *fastFlow,
                        std::vector<float> *slowFlow);
  void SaveStates(TimeVar *currentTime, char *statePath,
                  GridWriterFull *gridWriter);
  bool Route(float stepHours, std::vector<float> *fastFlow,
             std::vector<float> *slowFlow, std::vector<float> *discharge);
  float GetMaxSpeed() { return maxSpeed; }

private:
  void RouteInt(float stepSeconds, GridNode *node, KWGridNodeWavefront *cNode,
                float fastFlow, float slowFlow);
  void
  InitializeParameters(std::map<GaugeConfigSection *, float *> *paramSettings,
                       std::vector<FloatGrid *> *paramGrids);
  void InitializeRouting(float timeSeconds);
  // Build flat levelCells / levelOffsets once from downStreamNode DAG.
  void BuildRoutingLevels();

  std::vector<GridNode> *nodes;
  std::vector<KWGridNodeWavefront> kwNodes;
  // Flat wavefront storage (better cache than vector<vector>):
  // level L uses levelCells[levelOffsets[L] .. levelOffsets[L+1]).
  // Downstream cells always appear in a later level than their upstreams.
  std::vector<long> levelCells;
  std::vector<size_t> levelOffsets;
  float maxSpeed;
  bool initialized;
};

#endif
