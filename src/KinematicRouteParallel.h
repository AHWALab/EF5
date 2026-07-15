#ifndef KW_PARALLEL_MODEL_H
#define KW_PARALLEL_MODEL_H

#include "ModelBase.h"

enum KW_PARALLEL_LAYER
{
  KW_PAR_LAYER_FASTFLOW,
  KW_PAR_LAYER_INTERFLOW,
  KW_PAR_LAYER_QTY,
};

enum STATES_KW_PARALLEL
{
  STATE_KW_PAR_PQ,
  STATE_KW_PAR_PO,
  STATE_KW_PAR_IR,
  STATE_KW_PAR_QTY
};

struct KWGridNodeParallel : BasicGridNode
{
  float params[PARAM_KINEMATIC_QTY];
  float states[STATE_KW_PAR_QTY];

  bool channelGridCell;
  bool daActive;
  double slopeSqrt;
  double hillSlopeSqrt;

  double
      nexTime[KW_PAR_LAYER_QTY]; // This is a by product of computing cell routing
  KWGridNodeParallel
      *routeCNode[2][KW_PAR_LAYER_QTY]; // This is the node we route water to
  GridNode *routeNode[2][KW_PAR_LAYER_QTY];
  double routeAmount[2][KW_PAR_LAYER_QTY];
  double incomingWater[KW_PAR_LAYER_QTY];

  // double reservoirs[KW_PAR_LAYER_QTY]; // CREST has two excess storage reservoirs
  // (overland & interflow)

  // double previousStreamflow; //cms
  // double previousOverland;
  double incomingWaterOverland, incomingWaterChannel;
};

class KWRouteParallel : public RoutingModel
{

public:
  KWRouteParallel();
  ~KWRouteParallel();
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
  void RouteInt(float stepSeconds, GridNode *node, KWGridNodeParallel *cNode,
                float fastFlow, float slowFlow);
  void
  InitializeParameters(std::map<GaugeConfigSection *, float *> *paramSettings,
                       std::vector<FloatGrid *> *paramGrids);
  void InitializeRouting(float timeSeconds);

  std::vector<GridNode> *nodes;
  std::vector<KWGridNodeParallel> kwNodes;
  // Atomically filled during parallel routing; applied at the start of the
  // next Route() call so channel cells can consume it in RouteInt.
  std::vector<double> interflowIncomingNext;
  float maxSpeed;
  bool initialized;
};

#endif
