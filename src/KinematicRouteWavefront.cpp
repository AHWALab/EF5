#include "KinematicRouteWavefront.h"
#include "AscGrid.h"
#include "DatedName.h"
#include "GridWriterFull.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

/*
 * Wavefront OpenMP kinematic wave:
 * - Same Newton physics as serial KWRoute
 * - Same-step downhill handoff (atomics)
 * - Parallel only within independent topological levels
 */

static const char *stateStrings[] = {
    "pCQ",
    "pOQ",
    "IR",
};

static const float OVERLAND_BETA = 0.6f;

KWRouteWavefront::KWRouteWavefront()
    : nodes(NULL), maxSpeed(1.0f), initialized(false) {}

KWRouteWavefront::~KWRouteWavefront() {}

float KWRouteWavefront::SetObsInflow(long index, float inflow) {
  KWGridNodeWavefront *cNode = &(kwNodes[index]);
  GridNode *node = &nodes->at(index);
  float prev;
  if (!node->channelGridCell) {
    prev = cNode->states[STATE_KW_WF_PQ] * node->horLen;
    cNode->states[STATE_KW_WF_PO] = 0.0;
    cNode->states[STATE_KW_WF_PQ] = inflow * cNode->invHorLen;
    cNode->incomingWaterOverland = inflow * cNode->invHorLen;
    cNode->daActive = true;
  } else {
    prev = cNode->states[STATE_KW_WF_PQ];
    cNode->states[STATE_KW_WF_PO] = 0.0;
    cNode->states[STATE_KW_WF_PQ] = inflow;
    cNode->incomingWaterChannel = inflow;
    cNode->daActive = true;
  }
  return prev;
}

bool KWRouteWavefront::InitializeModel(
    std::vector<GridNode> *newNodes,
    std::map<GaugeConfigSection *, float *> *paramSettings,
    std::vector<FloatGrid *> *paramGrids) {

  nodes = newNodes;
  if (kwNodes.size() != nodes->size()) {
    kwNodes.resize(nodes->size());
  }

  size_t numNodes = nodes->size();
  for (size_t i = 0; i < numNodes; i++) {
    GridNode *node = &nodes->at(i);
    node->modelIndex = i;
    KWGridNodeWavefront *cNode = &(kwNodes[i]);
    cNode->slopeSqrt = pow(node->slope, 0.5f);
    cNode->hillSlopeSqrt = pow(node->slope * 0.5, 0.5f);
    cNode->incomingWater[KW_WF_LAYER_INTERFLOW] = 0.0;
    cNode->incomingWater[KW_WF_LAYER_FASTFLOW] = 0.0;
    cNode->incomingWaterOverland = 0.0;
    cNode->incomingWaterChannel = 0.0;
    cNode->daActive = false;
    cNode->invHorLen = (node->horLen > 0.0f) ? (1.0f / node->horLen) : 0.0f;
    cNode->alpha0 = 1.0f;
    cNode->alpha0Beta = OVERLAND_BETA;
    cNode->alphaCh = 1.0f;
    cNode->betaCh = OVERLAND_BETA;
    cNode->alphaBetaCh = OVERLAND_BETA;
    cNode->downStreamIndex = -1;
    for (int p = 0; p < STATE_KW_WF_QTY; p++) {
      cNode->states[p] = 0.0;
    }
  }

  InitializeParameters(paramSettings, paramGrids);
  initialized = false;
  maxSpeed = 1.0;

  return true;
}

void KWRouteWavefront::InitializeStates(TimeVar *beginTime, char *statePath,
                                        std::vector<float> *fastFlow,
                                        std::vector<float> *slowFlow) {
  (void)fastFlow;
  (void)slowFlow;

  DatedName timeStr;
  timeStr.SetNameStr("YYYYMMDD_HHUU");
  timeStr.ProcessNameLoose(NULL);
  timeStr.UpdateName(beginTime->GetTM());

  char buffer[300];
  for (int p = 0; p < STATE_KW_WF_QTY; p++) {
    sprintf(buffer, "%s/kwr_%s_%s.tif", statePath, stateStrings[p],
            timeStr.GetName());

    FloatGrid *sGrid = ReadFloatTifGrid(buffer);
    if (sGrid) {
      printf("Using Kinematic Wave Routing %s State Grid %s\n", stateStrings[p],
             buffer);
      if (g_DEM->IsSpatialMatch(sGrid)) {
        for (size_t i = 0; i < nodes->size(); i++) {
          GridNode *node = &nodes->at(i);
          KWGridNodeWavefront *cNode = &(kwNodes[i]);
          if (sGrid->data[node->y][node->x] != sGrid->noData) {
            cNode->states[p] = sGrid->data[node->y][node->x];
          }
        }
      } else {
        GridLoc pt;
        for (size_t i = 0; i < nodes->size(); i++) {
          GridNode *node = &(nodes->at(i));
          KWGridNodeWavefront *cNode = &(kwNodes[i]);
          if (sGrid->GetGridLoc(node->refLoc.x, node->refLoc.y, &pt) &&
              sGrid->data[pt.y][pt.x] != sGrid->noData) {
            cNode->states[p] = sGrid->data[pt.y][pt.x];
          }
        }
      }
      delete sGrid;
    } else {
      printf("Kinematic Wave Routing %s State Grid %s not found!\n",
             stateStrings[p], buffer);
    }
  }
}

void KWRouteWavefront::SaveStates(TimeVar *currentTime, char *statePath,
                                  GridWriterFull *gridWriter) {
  DatedName timeStr;
  timeStr.SetNameStr("YYYYMMDD_HHUU");
  timeStr.ProcessNameLoose(NULL);
  timeStr.UpdateName(currentTime->GetTM());

  std::vector<float> dataVals;
  dataVals.resize(nodes->size());

  char buffer[300];
  for (int p = 0; p < STATE_KW_WF_QTY; p++) {
    sprintf(buffer, "%s/kwr_%s_%s.tif", statePath, stateStrings[p],
            timeStr.GetName());
    for (size_t i = 0; i < nodes->size(); i++) {
      dataVals[i] = kwNodes[i].states[p];
    }
    gridWriter->WriteGrid(nodes, &dataVals, buffer, false);
  }
}

bool KWRouteWavefront::Route(float stepHours, std::vector<float> *fastFlow,
                             std::vector<float> *slowFlow,
                             std::vector<float> *discharge) {

  if (!initialized) {
    initialized = true;
    InitializeRouting(stepHours * 3600.0f);
    // Debug: dump wavefront levels + serial-order comparison, then stop.
    DumpRoutingScheduleAndExit();
  }

  const size_t numNodes = nodes->size();
  const float stepSeconds = stepHours * 3600.0f;
  const size_t numLevels =
      (levelOffsets.size() > 0) ? (levelOffsets.size() - 1) : 0;

  for (size_t level = 0; level < numLevels; level++) {
    const size_t begin = levelOffsets[level];
    const size_t end = levelOffsets[level + 1];
    const long numCells = static_cast<long>(end - begin);
#if _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (long c = 0; c < numCells; c++) {
      const long i = levelCells[begin + c];
      RouteInt(stepSeconds, &(nodes->at(i)), &(kwNodes[i]), fastFlow->at(i),
               slowFlow->at(i));
    }
  }

  for (size_t i = 0; i < numNodes; i++) {
    KWGridNodeWavefront *cNode = &(kwNodes[i]);
    slowFlow->at(i) = 0.0;
    fastFlow->at(i) = 0.0;
    cNode->incomingWaterOverland = 0.0;
    cNode->incomingWaterChannel = 0.0;
    cNode->daActive = false;
    if (!cNode->channelGridCell) {
      float q =
          cNode->incomingWater[KW_WF_LAYER_FASTFLOW] * nodes->at(i).horLen;
      q += (cNode->incomingWater[KW_WF_LAYER_INTERFLOW] * nodes->at(i).area /
            3.6);
      discharge->at(i) = q;
    } else {
      discharge->at(i) = cNode->incomingWater[KW_WF_LAYER_FASTFLOW];
    }
    if (!std::isfinite(discharge->at(i)) || discharge->at(i) < 0.0f) {
      discharge->at(i) = 0.0f;
    }
    cNode->states[STATE_KW_WF_IR] =
        cNode->states[STATE_KW_WF_IR] +
        cNode->incomingWater[KW_WF_LAYER_INTERFLOW];
    cNode->incomingWater[KW_WF_LAYER_INTERFLOW] = 0.0;
    cNode->incomingWater[KW_WF_LAYER_FASTFLOW] = 0.0;
  }

  return true;
}

void KWRouteWavefront::RouteInt(float stepSeconds, GridNode *node,
                                KWGridNodeWavefront *cNode, float fastFlow,
                                float slowFlow) {

  const float invHorLen = cNode->invHorLen;
  const float dtOverLen = stepSeconds * invHorLen;

  if (!cNode->channelGridCell) {
    const float beta = OVERLAND_BETA;
    const float alpha = cNode->alpha0;
    const float alphaBeta = cNode->alpha0Beta;

    fastFlow *= 0.001f; // mm to m
    const float newInWater = fastFlow;
    const float prevQ = cNode->states[STATE_KW_WF_PQ];

    float backDiffq = 0.0f;
    if (cNode->incomingWaterOverland + prevQ > 0.0) {
      backDiffq = pow((cNode->incomingWaterOverland + prevQ) * 0.5, beta - 1.0f);
      if (!std::isfinite(backDiffq)) {
        backDiffq = 0.0f;
      }
    }

    const float A = dtOverLen * cNode->incomingWaterOverland;
    const float B = alphaBeta * prevQ * backDiffq;
    const float C = stepSeconds * newInWater;
    const float D = dtOverLen;
    const float E = alphaBeta * backDiffq;
    float estq = (A + B + C) / (D + E);
    const float rhs = A + alpha * pow(prevQ, beta) + stepSeconds * newInWater;

    for (int itr = 0; itr < 10; itr++) {
      float resError = dtOverLen * estq + alpha * pow(estq, beta) - rhs;
      if (!std::isfinite(resError)) {
        resError = 0.0f;
      }
      if (fabsf(resError) < 0.01f) {
        break;
      }
      float resErrorD1 = dtOverLen + alphaBeta * pow(estq, beta - 1.0f);
      if (!std::isfinite(resErrorD1)) {
        resErrorD1 = 1.0f;
      }
      estq = estq - resError / resErrorD1;
      if (estq < 0.0f) {
        estq = 0.0f;
      }
    }
    if (estq < 0.0f || !std::isfinite(estq)) {
      estq = 0.0f;
    }

    const float newq = estq;
    cNode->states[STATE_KW_WF_PQ] = newq;

    if (cNode->downStreamIndex >= 0 &&
        !kwNodes[cNode->downStreamIndex].daActive) {
#if _OPENMP
#pragma omp atomic
#endif
      kwNodes[cNode->downStreamIndex].incomingWaterOverland += newq;
    }

    cNode->incomingWater[KW_WF_LAYER_FASTFLOW] = newq;

    cNode->states[STATE_KW_WF_IR] += slowFlow;
    double interflowLeak =
        cNode->states[STATE_KW_WF_IR] * cNode->params[PARAM_KINEMATIC_LEAKI];
    cNode->states[STATE_KW_WF_IR] -= interflowLeak;
    if (cNode->states[STATE_KW_WF_IR] < 0) {
      cNode->states[STATE_KW_WF_IR] = 0;
    }

    if (cNode->routeCNode[0][KW_WF_LAYER_INTERFLOW]) {
      double leakAmount =
          interflowLeak * cNode->routeAmount[0][KW_WF_LAYER_INTERFLOW] *
          node->area / cNode->routeNode[0][KW_WF_LAYER_INTERFLOW]->area;
      long target = cNode->routeNode[0][KW_WF_LAYER_INTERFLOW]->modelIndex;
#if _OPENMP
#pragma omp atomic
#endif
      kwNodes[target].incomingWater[KW_WF_LAYER_INTERFLOW] += leakAmount;
    }

    if (cNode->routeCNode[1][KW_WF_LAYER_INTERFLOW]) {
      double leakAmount =
          interflowLeak * cNode->routeAmount[1][KW_WF_LAYER_INTERFLOW] *
          node->area / cNode->routeNode[1][KW_WF_LAYER_INTERFLOW]->area;
      long target = cNode->routeNode[1][KW_WF_LAYER_INTERFLOW]->modelIndex;
#if _OPENMP
#pragma omp atomic
#endif
      kwNodes[target].incomingWater[KW_WF_LAYER_INTERFLOW] += leakAmount;
    }
  } else {
    const float beta0 = OVERLAND_BETA;
    const float alpha0 = cNode->alpha0;
    const float alpha0Beta = cNode->alpha0Beta;

    slowFlow += cNode->incomingWater[KW_WF_LAYER_INTERFLOW];
    fastFlow *= 0.001f;
    slowFlow *= 0.001f;
    const float newInWater = fastFlow + slowFlow;
    const float prevPO = cNode->states[STATE_KW_WF_PO];

    float backDiffq = 0.0f;
    if (cNode->incomingWaterOverland + prevPO > 0.0) {
      backDiffq =
          pow((cNode->incomingWaterOverland + prevPO) * 0.5, beta0 - 1.0f);
      if (!std::isfinite(backDiffq)) {
        backDiffq = 0.0f;
      }
    }

    float A = dtOverLen * cNode->incomingWaterOverland;
    float B = alpha0Beta * prevPO * backDiffq;
    float C = stepSeconds * newInWater;
    float D = dtOverLen;
    float E = alpha0Beta * backDiffq;
    float estq = (A + B + C) / (D + E);
    float rhs = A + alpha0 * pow(prevPO, beta0) + stepSeconds * newInWater;

    for (int itr = 0; itr < 10; itr++) {
      float resError = dtOverLen * estq + alpha0 * pow(estq, beta0) - rhs;
      if (!std::isfinite(resError)) {
        resError = 0.0f;
      }
      if (fabsf(resError) < 0.01f) {
        break;
      }
      float resErrorD1 = dtOverLen + alpha0Beta * pow(estq, beta0 - 1.0f);
      if (!std::isfinite(resErrorD1)) {
        resErrorD1 = 1.0f;
      }
      estq = estq - resError / resErrorD1;
      if (estq < 0.0f) {
        estq = 0.0f;
      }
    }
    if (estq < 0.0f || !std::isfinite(estq)) {
      estq = 0.0f;
    }
    const float newq = estq;
    cNode->states[STATE_KW_WF_PO] = newq;

    const float beta = cNode->betaCh;
    const float alpha = cNode->alphaCh;
    const float alphaBeta = cNode->alphaBetaCh;
    const float prevPQ = cNode->states[STATE_KW_WF_PQ];

    float backDiffQ = 0.0f;
    if (cNode->incomingWaterChannel + prevPQ > 0.0) {
      backDiffQ =
          pow((cNode->incomingWaterChannel + prevPQ) * 0.5, beta - 1.0f);
      if (!std::isfinite(backDiffQ)) {
        backDiffQ = 0.0f;
      }
    }

    A = dtOverLen * cNode->incomingWaterChannel;
    B = alphaBeta * prevPQ * backDiffQ;
    C = stepSeconds * newq;
    D = dtOverLen;
    E = alphaBeta * backDiffQ;
    float estQ = (A + B + C) / (D + E);
    rhs = A + alpha * pow(prevPQ, beta) + stepSeconds * newq;

    for (int itr = 0; itr < 10; itr++) {
      float resError = dtOverLen * estQ + alpha * pow(estQ, beta) - rhs;
      if (!std::isfinite(resError)) {
        resError = 0.0f;
      }
      if (fabsf(resError) < 0.01f) {
        break;
      }
      float resErrorD1 = dtOverLen + alphaBeta * pow(estQ, beta - 1.0f);
      if (!std::isfinite(resErrorD1)) {
        resErrorD1 = 1.0f;
      }
      estQ = estQ - resError / resErrorD1;
      if (estQ < 0.0f) {
        estQ = 0.0f;
      }
    }
    if (estQ < 0.0f || !std::isfinite(estQ)) {
      estQ = 0.0f;
    }
    const float newWater = estQ;

    cNode->states[STATE_KW_WF_PQ] = newWater;
    if (cNode->downStreamIndex >= 0 &&
        !kwNodes[cNode->downStreamIndex].daActive) {
#if _OPENMP
#pragma omp atomic
#endif
      kwNodes[cNode->downStreamIndex].incomingWaterChannel += newWater;
    }

    cNode->incomingWater[KW_WF_LAYER_FASTFLOW] = newWater;
    cNode->incomingWater[KW_WF_LAYER_INTERFLOW] = 0.0;
  }
}

void KWRouteWavefront::InitializeParameters(
    std::map<GaugeConfigSection *, float *> *paramSettings,
    std::vector<FloatGrid *> *paramGrids) {

  size_t numNodes = nodes->size();
  for (size_t i = 0; i < numNodes; i++) {
    GridNode *node = &nodes->at(i);
    KWGridNodeWavefront *cNode = &(kwNodes[i]);
    if (!node->gauge) {
      continue;
    }

    memcpy(cNode->params, (*paramSettings)[node->gauge],
           sizeof(float) * PARAM_KINEMATIC_QTY);

    if (!paramGrids->at(PARAM_KINEMATIC_ISU)) {
      cNode->states[STATE_KW_WF_IR] = cNode->params[PARAM_KINEMATIC_ISU];
    }
    cNode->incomingWater[KW_WF_LAYER_INTERFLOW] = 0.0;
    cNode->incomingWater[KW_WF_LAYER_FASTFLOW] = 0.0;

    GridLoc pt;
    for (size_t paramI = 0; paramI < PARAM_KINEMATIC_QTY; paramI++) {
      FloatGrid *grid = paramGrids->at(paramI);
      if (grid && g_DEM->IsSpatialMatch(grid)) {
        if (grid->data[node->y][node->x] == 0) {
          grid->data[node->y][node->x] = 0.01;
        }
        cNode->params[paramI] *= grid->data[node->y][node->x];
      } else if (grid &&
                 grid->GetGridLoc(node->refLoc.x, node->refLoc.y, &pt)) {
        if (grid->data[pt.y][pt.x] == 0) {
          grid->data[pt.y][pt.x] = 0.01;
        }
        cNode->params[paramI] *= grid->data[pt.y][pt.x];
      }
    }

    if (cNode->params[PARAM_KINEMATIC_LEAKI] < 0.0) {
      cNode->params[PARAM_KINEMATIC_LEAKI] = 0.0;
    } else if (cNode->params[PARAM_KINEMATIC_LEAKI] > 1.0) {
      cNode->params[PARAM_KINEMATIC_LEAKI] = 1.0;
    }

    if (cNode->params[PARAM_KINEMATIC_ALPHA] < 0.0) {
      cNode->params[PARAM_KINEMATIC_ALPHA] = 1.0;
    }

    if (cNode->params[PARAM_KINEMATIC_ALPHA0] < 0.0) {
      cNode->params[PARAM_KINEMATIC_ALPHA0] = 1.0;
    }

    if (cNode->params[PARAM_KINEMATIC_BETA] < 0.0) {
      cNode->params[PARAM_KINEMATIC_BETA] = 0.6;
    }

    if (node->fac > cNode->params[PARAM_KINEMATIC_TH]) {
      node->channelGridCell = true;
      cNode->channelGridCell = true;
    } else {
      node->channelGridCell = false;
      cNode->channelGridCell = false;
    }

    // Hot-path caches
    cNode->invHorLen = (node->horLen > 0.0f) ? (1.0f / node->horLen) : 0.0f;
    cNode->alpha0 = cNode->params[PARAM_KINEMATIC_ALPHA0];
    cNode->alpha0Beta = cNode->alpha0 * OVERLAND_BETA;
    cNode->alphaCh = cNode->params[PARAM_KINEMATIC_ALPHA];
    cNode->betaCh = cNode->params[PARAM_KINEMATIC_BETA];
    cNode->alphaBetaCh = cNode->alphaCh * cNode->betaCh;

    if (node->downStreamNode != INVALID_DOWNSTREAM_NODE &&
        node->downStreamNode < nodes->size()) {
      cNode->downStreamIndex =
          static_cast<long>(nodes->at(node->downStreamNode).modelIndex);
    } else {
      cNode->downStreamIndex = -1;
    }
  }
}

void KWRouteWavefront::InitializeRouting(float timeSeconds) {
  size_t numNodes = nodes->size();
  for (size_t i = 0; i < numNodes; i++) {
    GridNode *node = &nodes->at(i);
    KWGridNodeWavefront *cNode = &(kwNodes[i]);
    float speedUnder = cNode->params[PARAM_KINEMATIC_UNDER] * cNode->slopeSqrt;
    float nexTimeUnder = node->horLen / speedUnder;
    cNode->nexTime[KW_WF_LAYER_INTERFLOW] = nexTimeUnder;
  }

  for (size_t i = 0; i < numNodes; i++) {
    GridNode *currentNode, *previousNode;
    float currentSeconds, previousSeconds;
    GridNode *node = &nodes->at(i);
    KWGridNodeWavefront *cNode = &(kwNodes[i]);

    previousSeconds = 0;
    currentSeconds = 0;
    currentNode = node;
    previousNode = NULL;
    while (currentSeconds < timeSeconds && currentNode &&
           !kwNodes[currentNode->modelIndex].channelGridCell) {
      if (currentNode) {
        previousSeconds = currentSeconds;
        previousNode = currentNode;
        currentSeconds +=
            kwNodes[currentNode->modelIndex].nexTime[KW_WF_LAYER_INTERFLOW];
        if (currentNode->downStreamNode != INVALID_DOWNSTREAM_NODE) {
          currentNode = &(nodes->at(currentNode->downStreamNode));
        } else {
          currentNode = NULL;
        }
      } else {
        if (timeSeconds > currentSeconds) {
          previousNode = NULL;
        }
        break;
      }
    }

    cNode->routeNode[0][KW_WF_LAYER_INTERFLOW] = currentNode;
    cNode->routeCNode[0][KW_WF_LAYER_INTERFLOW] =
        (currentNode) ? &(kwNodes[currentNode->modelIndex]) : NULL;
    cNode->routeNode[1][KW_WF_LAYER_INTERFLOW] = previousNode;
    cNode->routeCNode[1][KW_WF_LAYER_INTERFLOW] =
        (previousNode) ? &(kwNodes[previousNode->modelIndex]) : NULL;
    if (currentNode && !kwNodes[currentNode->modelIndex].channelGridCell) {
      if ((currentSeconds - previousSeconds) > 0) {
        cNode->routeAmount[0][KW_WF_LAYER_INTERFLOW] =
            (timeSeconds - previousSeconds) /
            (currentSeconds - previousSeconds);
        cNode->routeAmount[1][KW_WF_LAYER_INTERFLOW] =
            1.0 - cNode->routeAmount[0][KW_WF_LAYER_INTERFLOW];
      }
    } else {
      cNode->routeAmount[0][KW_WF_LAYER_INTERFLOW] = 1.0;
      cNode->routeAmount[1][KW_WF_LAYER_INTERFLOW] = 0.0;
    }
  }

  BuildRoutingLevels();
}

void KWRouteWavefront::BuildRoutingLevels() {
  // Kahn/BFS wavefronts from the downhill DAG (downStreamNode).
  levelCells.clear();
  levelOffsets.clear();

  if (!nodes || nodes->empty()) {
    levelOffsets.push_back(0);
    return;
  }

  const size_t numNodes = nodes->size();
  levelCells.reserve(numNodes);
  levelOffsets.reserve(256);
  levelOffsets.push_back(0);

  std::vector<int> indegree(numNodes, 0);
  for (size_t i = 0; i < numNodes; i++) {
    const unsigned long ds = nodes->at(i).downStreamNode;
    if (ds != INVALID_DOWNSTREAM_NODE && ds < numNodes) {
      indegree[ds]++;
    }
  }

  std::vector<long> current;
  current.reserve(numNodes);
  for (size_t i = 0; i < numNodes; i++) {
    if (indegree[i] == 0) {
      current.push_back(static_cast<long>(i));
    }
  }

  size_t processed = 0;
  size_t maxWidth = 0;
  while (!current.empty()) {
    if (current.size() > maxWidth) {
      maxWidth = current.size();
    }
    for (size_t c = 0; c < current.size(); c++) {
      levelCells.push_back(current[c]);
    }
    levelOffsets.push_back(levelCells.size());
    processed += current.size();

    std::vector<long> next;
    next.reserve(current.size());
    for (size_t c = 0; c < current.size(); c++) {
      const unsigned long ds = nodes->at(current[c]).downStreamNode;
      if (ds != INVALID_DOWNSTREAM_NODE && ds < numNodes) {
        indegree[ds]--;
        if (indegree[ds] == 0) {
          next.push_back(static_cast<long>(ds));
        }
      }
    }
    current.swap(next);
  }

  if (processed != numNodes) {
    printf("KWRouteWavefront: flow network incomplete (%lu/%lu nodes in "
           "levels); falling back to linear order.\n",
           static_cast<unsigned long>(processed),
           static_cast<unsigned long>(numNodes));
    levelCells.resize(numNodes);
    levelOffsets.clear();
    levelOffsets.push_back(0);
    for (size_t i = 0; i < numNodes; i++) {
      levelCells[i] = static_cast<long>(numNodes - 1 - i);
    }
    levelOffsets.push_back(numNodes);
    return;
  }

  const size_t numLevels = levelOffsets.size() - 1;
  printf("KWRouteWavefront: %lu routing levels for %lu nodes "
         "(max wavefront width %lu).\n",
         static_cast<unsigned long>(numLevels),
         static_cast<unsigned long>(numNodes),
         static_cast<unsigned long>(maxWidth));
}

void KWRouteWavefront::DumpRoutingScheduleAndExit() {
  // Dumps GridNode maps + wavefront level (not kwNodes state).
  const size_t numNodes = nodes->size();
  const size_t numLevels =
      (levelOffsets.size() > 0) ? (levelOffsets.size() - 1) : 0;

  std::vector<float> levelId(numNodes, -1.0f);
  std::vector<float> serialOrder(numNodes);
  std::vector<float> nodeIndex(numNodes);
  std::vector<float> fac(numNodes);
  std::vector<float> channel(numNodes);
  std::vector<float> downStream(numNodes);

  for (size_t L = 0; L < numLevels; L++) {
    const size_t begin = levelOffsets[L];
    const size_t end = levelOffsets[L + 1];
    for (size_t p = begin; p < end; p++) {
      const long i = levelCells[p];
      if (i >= 0 && static_cast<size_t>(i) < numNodes) {
        levelId[i] = static_cast<float>(L);
      }
    }
  }

  for (size_t i = 0; i < numNodes; i++) {
    GridNode *node = &nodes->at(i);
    serialOrder[i] = static_cast<float>(numNodes - 1 - i);
    nodeIndex[i] = static_cast<float>(i);
    fac[i] = static_cast<float>(node->fac);
    channel[i] = node->channelGridCell ? 1.0f : 0.0f;
    downStream[i] = (node->downStreamNode == INVALID_DOWNSTREAM_NODE)
                        ? -1.0f
                        : static_cast<float>(node->downStreamNode);
  }

  GridWriterFull writer;
  writer.Initialize();
  writer.WriteGrid(nodes, &levelId, "kw_debug_wavefront_level.tif", false);
  writer.WriteGrid(nodes, &serialOrder, "kw_debug_serial_order.tif", false);
  writer.WriteGrid(nodes, &nodeIndex, "kw_debug_node_index.tif", false);
  writer.WriteGrid(nodes, &fac, "kw_debug_fac.tif", false);
  writer.WriteGrid(nodes, &channel, "kw_debug_channel.tif", false);
  writer.WriteGrid(nodes, &downStream, "kw_debug_downstream.tif", false);

  FILE *widths = fopen("kw_debug_level_widths.csv", "w");
  if (widths) {
    fprintf(widths, "level,cell_count\n");
    for (size_t L = 0; L < numLevels; L++) {
      const size_t count = levelOffsets[L + 1] - levelOffsets[L];
      fprintf(widths, "%lu,%lu\n", static_cast<unsigned long>(L),
              static_cast<unsigned long>(count));
    }
    fclose(widths);
  }

  FILE *fp = fopen("kw_debug_wavefront_summary.txt", "w");
  if (fp) {
    fprintf(fp, "router=KWRouteWavefront\n");
    fprintf(fp, "numNodes=%lu\n", static_cast<unsigned long>(numNodes));
    fprintf(fp, "numLevels=%lu\n", static_cast<unsigned long>(numLevels));
    fprintf(fp, "levelCells.size=%lu\n",
            static_cast<unsigned long>(levelCells.size()));
    fprintf(fp, "levelOffsets.size=%lu (numLevels+1)\n",
            static_cast<unsigned long>(levelOffsets.size()));
    fprintf(fp,
            "level L cells = levelCells[levelOffsets[L] .. levelOffsets[L+1])\n");
    fprintf(fp, "files=\n");
    fprintf(fp, "  kw_debug_wavefront_level.tif\n");
    fprintf(fp, "  kw_debug_serial_order.tif\n");
    fprintf(fp, "  kw_debug_node_index.tif\n");
    fprintf(fp, "  kw_debug_fac.tif\n");
    fprintf(fp, "  kw_debug_channel.tif\n");
    fprintf(fp, "  kw_debug_downstream.tif\n");
    fprintf(fp, "  kw_debug_level_widths.csv\n");
    fclose(fp);
  }

  printf("KWRouteWavefront DEBUG: wrote level/schedule GeoTIFFs + "
         "kw_debug_level_widths.csv (%lu nodes, %lu levels). Exiting.\n",
         static_cast<unsigned long>(numNodes),
         static_cast<unsigned long>(numLevels));
  fflush(stdout);
  exit(0);
}
