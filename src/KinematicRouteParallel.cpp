#include "KinematicRouteParallel.h"
#include "AscGrid.h"
#include "DatedName.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static const char *stateStrings[] = {
    "pCQ",
    "pOQ",
    "IR",
};

KWRouteParallel::KWRouteParallel() {}

KWRouteParallel::~KWRouteParallel() {}

float KWRouteParallel::SetObsInflow(long index, float inflow)
{
  KWGridNodeParallel *cNode = &(kwNodes[index]);
  GridNode *node = &nodes->at(index);
  float prev;
  if (!node->channelGridCell)
  {
    prev = cNode->states[STATE_KW_PAR_PQ] * node->horLen;
    cNode->states[STATE_KW_PAR_PO] = 0.0;
    cNode->states[STATE_KW_PAR_PQ] = inflow / node->horLen;
    cNode->incomingWaterOverland = inflow / node->horLen;
    cNode->daActive = true;
  }
  else
  {
    prev = cNode->states[STATE_KW_PAR_PQ];
    cNode->states[STATE_KW_PAR_PO] = 0.0;
    cNode->states[STATE_KW_PAR_PQ] = inflow;
    cNode->incomingWaterChannel = inflow;
    cNode->daActive = true;
  }
  return prev;
}

bool KWRouteParallel::InitializeModel(
    std::vector<GridNode> *newNodes,
    std::map<GaugeConfigSection *, float *> *paramSettings,
    std::vector<FloatGrid *> *paramGrids)
{

  nodes = newNodes;
  if (kwNodes.size() != nodes->size())
  {
    kwNodes.resize(nodes->size());
  }
  interflowIncomingNext.assign(nodes->size(), 0.0);

  // Fill in modelIndex in the gridNodes
  size_t numNodes = nodes->size();
  for (size_t i = 0; i < numNodes; i++)
  {
    GridNode *node = &nodes->at(i);
    node->modelIndex = i;
    KWGridNodeParallel *cNode = &(kwNodes[i]);
    cNode->slopeSqrt = pow(node->slope, 0.5f);
    cNode->hillSlopeSqrt = pow(node->slope * 0.5, 0.5f);
    cNode->incomingWater[KW_PAR_LAYER_INTERFLOW] = 0.0;
    cNode->incomingWater[KW_PAR_LAYER_FASTFLOW] = 0.0;
    cNode->incomingWaterOverland = 0.0;
    cNode->incomingWaterChannel = 0.0;
    cNode->daActive = false;
    for (int p = 0; p < STATE_KW_PAR_QTY; p++)
    {
      cNode->states[p] = 0.0;
    }
  }

  InitializeParameters(paramSettings, paramGrids);
  initialized = false;
  maxSpeed = 1.0;

  return true;
}

void KWRouteParallel::InitializeStates(TimeVar *beginTime, char *statePath,
                                       std::vector<float> *fastFlow,
                                       std::vector<float> *slowFlow)
{
  DatedName timeStr;
  timeStr.SetNameStr("YYYYMMDD_HHUU");
  timeStr.ProcessNameLoose(NULL);
  timeStr.UpdateName(beginTime->GetTM());

  char buffer[300];
  for (int p = 0; p < STATE_KW_PAR_QTY; p++)
  {
    sprintf(buffer, "%s/kwr_%s_%s.tif", statePath, stateStrings[p],
            timeStr.GetName());

    FloatGrid *sGrid = ReadFloatTifGrid(buffer);
    if (sGrid)
    {
      printf("Using Kinematic Wave Routing %s State Grid %s\n", stateStrings[p],
             buffer);
      if (g_DEM->IsSpatialMatch(sGrid))
      {
        for (size_t i = 0; i < nodes->size(); i++)
        {
          GridNode *node = &nodes->at(i);
          KWGridNodeParallel *cNode = &(kwNodes[i]);
          if (sGrid->data[node->y][node->x] != sGrid->noData)
          {
            cNode->states[p] = sGrid->data[node->y][node->x];
          }
        }
      }
      else
      {
        GridLoc pt;
        for (size_t i = 0; i < nodes->size(); i++)
        {
          GridNode *node = &(nodes->at(i));
          KWGridNodeParallel *cNode = &(kwNodes[i]);
          if (sGrid->GetGridLoc(node->refLoc.x, node->refLoc.y, &pt) &&
              sGrid->data[pt.y][pt.x] != sGrid->noData)
          {
            cNode->states[p] = sGrid->data[pt.y][pt.x];
          }
        }
      }
      delete sGrid;
    }
    else
    {
      printf("Kinematic Wave Routing %s State Grid %s not found!\n",
             stateStrings[p], buffer);
    }
  }
}

void KWRouteParallel::SaveStates(TimeVar *currentTime, char *statePath,
                                 GridWriterFull *gridWriter)
{
  DatedName timeStr;
  timeStr.SetNameStr("YYYYMMDD_HHUU");
  timeStr.ProcessNameLoose(NULL);
  timeStr.UpdateName(currentTime->GetTM());

  std::vector<float> dataVals;
  dataVals.resize(nodes->size());

  char buffer[300];
  for (int p = 0; p < STATE_KW_PAR_QTY; p++)
  {
    sprintf(buffer, "%s/kwr_%s_%s.tif", statePath, stateStrings[p],
            timeStr.GetName());
    for (size_t i = 0; i < nodes->size(); i++)
    {
      KWGridNodeParallel *cNode = &(kwNodes[i]);
      dataVals[i] = cNode->states[p];
    }
    gridWriter->WriteGrid(nodes, &dataVals, buffer, false);
  }
}

bool KWRouteParallel::Route(float stepHours, std::vector<float> *fastFlow,
                            std::vector<float> *slowFlow,
                            std::vector<float> *discharge)
{

  if (!initialized)
  {
    initialized = true;
    InitializeRouting(stepHours * 3600.0f);
  }

  size_t numNodes = nodes->size();

  // Interflow leaks written last step are available for this step's RouteInt
  // (especially channel cells that consume them as lateral inflow).
  for (size_t i = 0; i < numNodes; i++)
  {
    kwNodes[i].incomingWater[KW_PAR_LAYER_INTERFLOW] +=
        interflowIncomingNext[i];
    interflowIncomingNext[i] = 0.0;
  }

#if _OPENMP
#pragma omp parallel for
#endif
  for (long i = numNodes - 1; i >= 0; i--)
  {
    KWGridNodeParallel *cNode = &(kwNodes[i]);
    RouteInt(stepHours * 3600.0f, &(nodes->at(i)), cNode, fastFlow->at(i),
             slowFlow->at(i));
  }

  // Deposit surface outflows for the next timestep and build discharge.
  for (size_t i = 0; i < numNodes; i++)
  {
    KWGridNodeParallel *cNode = &(kwNodes[i]);
    GridNode *node = &nodes->at(i);

    slowFlow->at(i) = 0.0; // cNode->incomingWater[KW_PAR_LAYER_INTERFLOW];
    fastFlow->at(i) = 0.0; // cNode->incomingWater[KW_PAR_LAYER_FASTFLOW];

    /* Collect water for next time step */
    // cNode->incomingWaterOverland = 0.0;
    // cNode->incomingWaterChannel = 0.0;
    if (!cNode->channelGridCell)
    {
      /**** Overland Node ***/
      if (node->downStreamNode != INVALID_DOWNSTREAM_NODE && !kwNodes[nodes->at(node->downStreamNode).modelIndex].daActive)
      {
        // Add water from update state
        kwNodes[nodes->at(node->downStreamNode).modelIndex].incomingWaterOverland += cNode->states[STATE_KW_PAR_PQ];
      }
    }
    else
    {
      /**** Channel Node ***/
      // First do overland routing within channel pixel
      if (node->downStreamNode != INVALID_DOWNSTREAM_NODE && !kwNodes[nodes->at(node->downStreamNode).modelIndex].daActive)
      {
        kwNodes[nodes->at(node->downStreamNode).modelIndex]
            .incomingWaterChannel += cNode->states[STATE_KW_PAR_PQ];
      }
    }
    cNode->daActive = false;
    if (!cNode->channelGridCell)
    {
      float q = cNode->incomingWater[KW_PAR_LAYER_FASTFLOW] * nodes->at(i).horLen;
      q += (cNode->incomingWater[KW_PAR_LAYER_INTERFLOW] * nodes->at(i).area / 3.6);
      discharge->at(i) = q; // * (stepHours * 3600.0f);
    }
    else
    {
      discharge->at(i) = cNode->incomingWater[KW_PAR_LAYER_FASTFLOW];
    }
    cNode->states[STATE_KW_PAR_IR] =
        cNode->states[STATE_KW_PAR_IR] + cNode->incomingWater[KW_PAR_LAYER_INTERFLOW];
    cNode->incomingWater[KW_PAR_LAYER_INTERFLOW] =
        0.0; // Zero here so we can save states
    cNode->incomingWater[KW_PAR_LAYER_FASTFLOW] =
        0.0; // Zero here so we can save states
  }

  // InitializeRouting(stepHours * 3600.0f);

  return true;
}

void KWRouteParallel::RouteInt(float stepSeconds, GridNode *node, KWGridNodeParallel *cNode,
                               float fastFlow, float slowFlow)
{

  if (!cNode->channelGridCell)
  {
    /**** Overland Routing ***/
    float beta = 5.0 / 3.0;
    // Temporary conversion because Implicit KW parameters are computed differently
    // This should be done outside the loop (or outside EF5 even better)
    float alpha = pow(1.0 / cNode->params[PARAM_KINEMATIC_ALPHA0], beta);

    fastFlow /= 1000.0;          // mm to m
    float newInWater = fastFlow; // / node->horLen;

    float prevQ = cNode->states[STATE_KW_PAR_PQ];
    if (!std::isfinite(prevQ) || prevQ < 0.0f)
    {
      prevQ = 0.0f;
      cNode->states[STATE_KW_PAR_PQ] = 0.0f;
    }

    float A, B, C, D, E;

    // Compute different terms separate for convenience and readibility
    A = pow((1.0 / alpha) * prevQ, 1.0 / beta);
    B = stepSeconds * newInWater;
    C = stepSeconds / node->horLen;
    D = prevQ;
    E = cNode->incomingWaterOverland;

    float newh = A + B - C * (D - E); // Mean overland flow depth (m/s)
    if (!std::isfinite(newh) || newh < 0.0f)
    {
      newh = 0.0f;
    }
    float newq = alpha * pow(newh, beta);
    if (!std::isfinite(newq) || newq < 0.0f)
    {
      newq = 0.0f;
    }

    cNode->states[STATE_KW_PAR_PQ] = newq;
    /* if (node->downStreamNode != INVALID_DOWNSTREAM_NODE && !kwNodes[nodes->at(node->downStreamNode).modelIndex].daActive) {
      // Add water from current state (not the newly calculated)
      kwNodes[nodes->at(node->downStreamNode).modelIndex]
          .incomingWaterOverland += cNode->states[STATE_KW_PAR_PQ]; //newq; **** The New Water seems to be changing the Q/q for downstream values. This is how the original scheme is supposed to work, but the explicit scheme requires that the state variables of the previous time step do not change //
    } */
    // Trying doing this outside for parallelization

    // Reset for next time step
    cNode->incomingWaterOverland = 0.0;

    cNode->incomingWater[KW_PAR_LAYER_FASTFLOW] = newq;

    // Add Interflow Excess Water to Reservoir
    cNode->states[STATE_KW_PAR_IR] += slowFlow;
    double interflowLeak =
        cNode->states[STATE_KW_PAR_IR] * cNode->params[PARAM_KINEMATIC_LEAKI];
    // printf(" %f ", interflowLeak);
    cNode->states[STATE_KW_PAR_IR] -= interflowLeak;
    if (cNode->states[STATE_KW_PAR_IR] < 0)
    {
      cNode->states[STATE_KW_PAR_IR] = 0;
    }

    if (cNode->routeCNode[0][KW_PAR_LAYER_INTERFLOW])
    {
      double interflowLeak0 =
          interflowLeak * cNode->routeAmount[0][KW_PAR_LAYER_INTERFLOW] *
          node->area / cNode->routeNode[0][KW_PAR_LAYER_INTERFLOW]->area;
      double leakAmount = interflowLeak0;
      /*if (cNode->routeNode[0][KW_PAR_LAYER_INTERFLOW]->channelGridCell) {
      printf(" 0 got %f ",
      cNode->routeCNode[0][KW_PAR_LAYER_INTERFLOW]->incomingWater[KW_PAR_LAYER_INTERFLOW]);
      }*/
      long targetIndex =
          cNode->routeNode[0][KW_PAR_LAYER_INTERFLOW]->modelIndex;
#if _OPENMP
#pragma omp atomic update
#endif
      interflowIncomingNext[targetIndex] += leakAmount;
    }

    if (cNode->routeCNode[1][KW_PAR_LAYER_INTERFLOW])
    {
      double interflowLeak1 =
          interflowLeak * cNode->routeAmount[1][KW_PAR_LAYER_INTERFLOW] *
          node->area / cNode->routeNode[1][KW_PAR_LAYER_INTERFLOW]->area;
      double leakAmount = interflowLeak1;
      // printf(" 1 got %f ", leakAmount);
      long targetIndex =
          cNode->routeNode[1][KW_PAR_LAYER_INTERFLOW]->modelIndex;
#if _OPENMP
#pragma omp atomic update
#endif
      interflowIncomingNext[targetIndex] += leakAmount;
    }
  }
  else
  {

    /**** Channel Routing ***/
    // First do overland routing within channel pixel

    float beta = 5.0 / 3.0;
    float alpha = pow(1.0 / cNode->params[PARAM_KINEMATIC_ALPHA0], beta);
    slowFlow += cNode->incomingWater[KW_PAR_LAYER_INTERFLOW];
    // printf(" %f ", cNode->incomingWater[KW_PAR_LAYER_INTERFLOW]);
    fastFlow /= 1000.0; // mm to m
    slowFlow /= 1000.0; // mm to m
    float newInWater = (fastFlow + slowFlow);

    float prevPO = cNode->states[STATE_KW_PAR_PO];
    float prevPQ = cNode->states[STATE_KW_PAR_PQ];
    if (!std::isfinite(prevPO) || prevPO < 0.0f)
    {
      prevPO = 0.0f;
      cNode->states[STATE_KW_PAR_PO] = 0.0f;
    }
    if (!std::isfinite(prevPQ) || prevPQ < 0.0f)
    {
      prevPQ = 0.0f;
      cNode->states[STATE_KW_PAR_PQ] = 0.0f;
    }

    float A, B, C, D, E;

    // Compute different terms separate for convenience and readibility
    A = pow((1.0 / alpha) * prevPO, 1.0 / beta);
    B = stepSeconds * newInWater;
    C = stepSeconds / node->horLen;
    D = prevPO;
    E = cNode->incomingWaterOverland;

    float newh = A + B - C * (D - E); // Mean overland flow depth (m/s)
    if (!std::isfinite(newh) || newh < 0.0f)
    {
      newh = 0.0f;
    }
    float newq = alpha * pow(newh, beta);
    if (!std::isfinite(newq) || newq < 0.0f)
    {
      newq = 0.0f;
    }

    // Here we compute channel routing
    // This should be done outside the loop (or outside EF5 even better)
    beta = 1.0 / cNode->params[PARAM_KINEMATIC_BETA];
    alpha = pow(1.0 / cNode->params[PARAM_KINEMATIC_ALPHA], beta);

    // Channel Flow
    // Compute Q at current grid point
    A = pow((1.0 / alpha) * prevPQ, 1.0 / beta);
    B = stepSeconds * newq; // same-cell overland result feeds channel this step
    C = stepSeconds / node->horLen;
    D = prevPQ;
    E = cNode->incomingWaterChannel;

    float estA = A + B - C * (D - E);
    if (!std::isfinite(estA) || estA < 0.0f)
    {
      estA = 0.0f;
    }
    float newWater = alpha * pow(estA, beta);
    if (!std::isfinite(newWater) || newWater < 0.0f)
    {
      newWater = 0.0f;
    }

    /*if (newWater != newWater) {
    printf("New water is %f (%f, %f) %f %f [%f %f %f %f %f] %f %f\n", newWater,
    cNode->incomingWaterChannel, cNode->states[STATE_KW_PAR_PQ], newq,
    cNode->incomingWaterOverland, A, B, C, D, E, alpha, 0.0);
    }*/

    // Update for next time step
    cNode->incomingWaterChannel = 0.0;
    cNode->incomingWaterOverland = 0.0;

    cNode->states[STATE_KW_PAR_PQ] =
        newWater; // Update previous Q for further routing if "steps" > 1

    cNode->states[STATE_KW_PAR_PO] = newq;
    /* if (node->downStreamNode != INVALID_DOWNSTREAM_NODE && !kwNodes[nodes->at(node->downStreamNode).modelIndex].daActive) {
      kwNodes[nodes->at(node->downStreamNode).modelIndex]
          .incomingWaterChannel += newWater;
    } */
    // Trying to do this outside for parallelization

    cNode->incomingWater[KW_PAR_LAYER_FASTFLOW] = newWater;
    cNode->incomingWater[KW_PAR_LAYER_INTERFLOW] = 0.0;
  }
}

void KWRouteParallel::InitializeParameters(
    std::map<GaugeConfigSection *, float *> *paramSettings,
    std::vector<FloatGrid *> *paramGrids)
{

  // This pass distributes parameters
  size_t numNodes = nodes->size();
  for (size_t i = 0; i < numNodes; i++)
  {
    GridNode *node = &nodes->at(i);
    KWGridNodeParallel *cNode = &(kwNodes[i]);
    if (!node->gauge)
    {
      continue;
    }
    // Copy all of the parameters over
    memcpy(cNode->params, (*paramSettings)[node->gauge],
           sizeof(float) * PARAM_KINEMATIC_QTY);

    if (!paramGrids->at(PARAM_KINEMATIC_ISU))
    {
      cNode->states[STATE_KW_PAR_IR] = cNode->params[PARAM_KINEMATIC_ISU];
    }
    cNode->incomingWater[KW_PAR_LAYER_INTERFLOW] = 0.0;
    cNode->incomingWater[KW_PAR_LAYER_FASTFLOW] = 0.0;

    // Deal with the distributed parameters here
    GridLoc pt;
    for (size_t paramI = 0; paramI < PARAM_KINEMATIC_QTY; paramI++)
    {
      FloatGrid *grid = paramGrids->at(paramI);
      if (grid && g_DEM->IsSpatialMatch(grid))
      {
        if (grid->data[node->y][node->x] == 0)
        {
          grid->data[node->y][node->x] = 0.01;
        }
        cNode->params[paramI] *= grid->data[node->y][node->x];
      }
      else if (grid &&
               grid->GetGridLoc(node->refLoc.x, node->refLoc.y, &pt))
      {
        if (grid->data[pt.y][pt.x] == 0)
        {
          grid->data[pt.y][pt.x] = 0.01;
          // printf("Using nodata value in param %s\n",
          // modelParamStrings[MODEL_CREST][paramI]);
        }
        cNode->params[paramI] *= grid->data[pt.y][pt.x];
      }
    }

    if (cNode->params[PARAM_KINEMATIC_LEAKI] < 0.0)
    {
      // printf("Node Leak Interflow(%f) is less than 0, setting to 0.\n",
      // cNode->params[PARAM_KINEMATIC_LEAKI]);
      cNode->params[PARAM_KINEMATIC_LEAKI] = 0.0;
    }
    else if (cNode->params[PARAM_KINEMATIC_LEAKI] > 1.0)
    {
      // printf("Node Leak Interflow(%f) is greater than 1, setting to 1.\n",
      // cNode->params[PARAM_KINEMATIC_LEAKI]);
      cNode->params[PARAM_KINEMATIC_LEAKI] = 1.0;
    }

    if (cNode->params[PARAM_KINEMATIC_ALPHA] < 0.0)
    {
      // printf("Node Alpha(%f) is less than 0, setting to 1.\n",
      // cNode->params[PARAM_KINEMATIC_ALPHA]);
      cNode->params[PARAM_KINEMATIC_ALPHA] = 1.0;
    }

    if (cNode->params[PARAM_KINEMATIC_ALPHA0] < 0.0)
    {
      // printf("Node Alpha0(%f) is less than 0, setting to 1.\n",
      // cNode->params[PARAM_KINEMATIC_ALPHA0]);
      cNode->params[PARAM_KINEMATIC_ALPHA0] = 1.0;
    }

    if (cNode->params[PARAM_KINEMATIC_BETA] < 0.0)
    {
      // printf("Node Beta(%f) is less than 0, setting to 0.6.\n",
      // cNode->params[PARAM_KINEMATIC_BETA]);
      cNode->params[PARAM_KINEMATIC_BETA] = 0.6;
    }

    if (node->fac > cNode->params[PARAM_KINEMATIC_TH])
    {
      node->channelGridCell = true;
      cNode->channelGridCell = true;
    }
    else
    {
      node->channelGridCell = false;
      cNode->channelGridCell = false;
    }
  }
}

void KWRouteParallel::InitializeRouting(float timeSeconds)
{

  // This pass distributes parameters & calculates the time it takes for water
  // to cross the grid cell.
  size_t numNodes = nodes->size();
  for (size_t i = 0; i < numNodes; i++)
  {
    GridNode *node = &nodes->at(i);
    KWGridNodeParallel *cNode = &(kwNodes[i]);

    // Calculate the water speed for interflow
    float speedUnder = cNode->params[PARAM_KINEMATIC_UNDER] * cNode->slopeSqrt;

    float nexTimeUnder = node->horLen / speedUnder;
    cNode->nexTime[KW_PAR_LAYER_INTERFLOW] = nexTimeUnder;
  }

  // This pass figures out which cell water is routed to
  for (size_t i = 0; i < numNodes; i++)
  {
    GridNode *currentNode, *previousNode;
    float currentSeconds, previousSeconds;
    GridNode *node = &nodes->at(i);
    KWGridNodeParallel *cNode = &(kwNodes[i]);

    // Interflow routing
    previousSeconds = 0;
    currentSeconds = 0;
    currentNode = node;
    previousNode = NULL;
    while (currentSeconds < timeSeconds && currentNode &&
           !kwNodes[currentNode->modelIndex].channelGridCell)
    {
      if (currentNode)
      {
        previousSeconds = currentSeconds;
        previousNode = currentNode;
        currentSeconds +=
            kwNodes[currentNode->modelIndex].nexTime[KW_PAR_LAYER_INTERFLOW];
        if (currentNode->downStreamNode != INVALID_DOWNSTREAM_NODE)
        {
          currentNode = &(nodes->at(currentNode->downStreamNode));
        }
        else
        {
          currentNode = NULL;
        }
      }
      else
      {
        if (timeSeconds > currentSeconds)
        {
          previousNode = NULL;
        }
        break; // We have effectively run out of nodes to transverse, this is
               // done!
      }
    }

    cNode->routeNode[0][KW_PAR_LAYER_INTERFLOW] = currentNode;
    cNode->routeCNode[0][KW_PAR_LAYER_INTERFLOW] =
        (currentNode) ? &(kwNodes[currentNode->modelIndex]) : NULL;
    cNode->routeNode[1][KW_PAR_LAYER_INTERFLOW] = previousNode;
    cNode->routeCNode[1][KW_PAR_LAYER_INTERFLOW] =
        (previousNode) ? &(kwNodes[previousNode->modelIndex]) : NULL;
    if (currentNode && !kwNodes[currentNode->modelIndex].channelGridCell)
    {
      if ((currentSeconds - previousSeconds) > 0)
      {
        cNode->routeAmount[0][KW_PAR_LAYER_INTERFLOW] =
            (timeSeconds - previousSeconds) /
            (currentSeconds - previousSeconds);
        cNode->routeAmount[1][KW_PAR_LAYER_INTERFLOW] =
            1.0 - cNode->routeAmount[0][KW_PAR_LAYER_INTERFLOW];
      }
    }
    else
    {
      cNode->routeAmount[0][KW_PAR_LAYER_INTERFLOW] = 1.0;
      cNode->routeAmount[1][KW_PAR_LAYER_INTERFLOW] = 0.0;
    }
  }
}
