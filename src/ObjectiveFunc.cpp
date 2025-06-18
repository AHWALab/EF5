#include "ObjectiveFunc.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

const char *objectiveStrings[] = {"nsce", "cc", "sse", "mkge"};

const OBJECTIVE_GOAL objectiveGoals[] = {
    OBJECTIVE_GOAL_MAXIMIZE, // nsce
    OBJECTIVE_GOAL_MAXIMIZE, // cc
    OBJECTIVE_GOAL_MINIMIZE, // sse
    OBJECTIVE_GOAL_MAXIMIZE  // mkge
};

// Internal functions for calculating actual objective function scores
static float CalcNSCE(std::vector<float> *obs, std::vector<float> *sim);
static float CalcCC(std::vector<float> *obs, std::vector<float> *sim);
static float CalcSSE(std::vector<float> *obs, std::vector<float> *sim);
static float CalcMKGE(std::vector<float> *obs, std::vector<float> *sim);

// This is the main function for calculating objective functions, everything
// passes through here first.
float CalcObjFunc(std::vector<float> *obs, std::vector<float> *sim,
                  OBJECTIVES obj) {

  switch (obj) {
  case OBJECTIVE_NSCE:
    return CalcNSCE(obs, sim);
  case OBJECTIVE_CC:
    return CalcCC(obs, sim);
  case OBJECTIVE_SSE:
    return CalcSSE(obs, sim);
  case OBJECTIVE_MKGE:
    return CalcMKGE(obs, sim);
  default:
    return 0;
  }
}

float CalcNSCE(std::vector<float> *obs, std::vector<float> *sim) {

  float obsMean = 0, obsAcc = 0, simAcc = 0, validQs = 0;
  size_t totalTimeSteps = obs->size();
  for (size_t tsIndex = 0; tsIndex < totalTimeSteps; tsIndex++) {
    if ((*obs)[tsIndex] == (*obs)[tsIndex] &&
        (*sim)[tsIndex] == (*sim)[tsIndex]) {
      obsMean += (*obs)[tsIndex];
      validQs++;
    }
  }

  obsMean /= validQs;

  for (size_t tsIndex = 0; tsIndex < totalTimeSteps; tsIndex++) {
    if ((*obs)[tsIndex] == (*obs)[tsIndex] &&
        (*sim)[tsIndex] == (*sim)[tsIndex]) {
      // printf("%f %f\n", (*obs)[tsIndex], (*sim)[tsIndex]);
      obsAcc += powf((*obs)[tsIndex] - obsMean, 2.0);
      simAcc += powf((*obs)[tsIndex] - (*sim)[tsIndex], 2.0);
    }
  }

  float result = 1.0 - (simAcc / obsAcc);
  if (result == result) {
    return result;
  } else {
    return -10000000000.0;
  }
}

float CalcCC(std::vector<float> *obs, std::vector<float> *sim) {

  float obsMean = 0, simMean = 0, obsAcc2 = 0, obsAcc = 0, simAcc = 0;
  size_t validQs = 0, totalTimeSteps = obs->size();
  for (size_t tsIndex = 0; tsIndex < totalTimeSteps; tsIndex++) {
    if ((*obs)[tsIndex] == (*obs)[tsIndex] &&
        (*sim)[tsIndex] == (*sim)[tsIndex]) {
      obsMean += (*obs)[tsIndex];
      simMean += (*sim)[tsIndex];
      validQs++;
    }
  }

  obsMean /= validQs;
  simMean /= validQs;

  for (size_t tsIndex = 0; tsIndex < totalTimeSteps; tsIndex++) {
    if ((*obs)[tsIndex] == (*obs)[tsIndex] &&
        (*sim)[tsIndex] == (*sim)[tsIndex]) {
      obsAcc += pow((*obs)[tsIndex] - obsMean, 2);
      simAcc += pow((*sim)[tsIndex] - simMean, 2);
      obsAcc2 += (((*obs)[tsIndex] - obsMean) * ((*sim)[tsIndex] - simMean));
    }
  }

  obsAcc = sqrt(obsAcc);
  simAcc = sqrt(simAcc);

  return obsAcc2 / (obsAcc * simAcc);
}

float CalcSSE(std::vector<float> *obs, std::vector<float> *sim) {

  float sse = 0;
  size_t totalTimeSteps = obs->size();

  for (size_t tsIndex = 0; tsIndex < totalTimeSteps; tsIndex++) {
    if ((*obs)[tsIndex] == (*obs)[tsIndex] &&
        (*sim)[tsIndex] == (*sim)[tsIndex]) {
      sse += pow((*obs)[tsIndex] - (*sim)[tsIndex], 2);
    }
  }

  return sse;
}

static float CalcMKGE(std::vector<float> *obs, std::vector<float> *sim)
{
    size_t totalTimeSteps = obs->size();
    float sum_obs = 0, sum_sim = 0;
    size_t validQs = 0;

    // Calculate sums and count valid data points
    for (size_t tsIndex = 0; tsIndex < totalTimeSteps; tsIndex++)
    {
        if ((*obs)[tsIndex] == (*obs)[tsIndex] && (*sim)[tsIndex] == (*sim)[tsIndex])
        { // NaN check
            sum_obs += (*obs)[tsIndex];
            sum_sim += (*sim)[tsIndex];
            validQs++;
        }
    }

    // Handle insufficient data
    if (validQs < 2)
    {
        return -10000000000.0f;
    }

    // Calculate means
    float Y_obs = sum_obs / validQs;
    float Y_sim = sum_sim / validQs;

    // Calculate variance, covariance, and correlation
    float sum_obs_sq = 0, sum_sim_sq = 0, sum_cov = 0;
    for (size_t tsIndex = 0; tsIndex < totalTimeSteps; tsIndex++)
    {
        if ((*obs)[tsIndex] == (*obs)[tsIndex] && (*sim)[tsIndex] == (*sim)[tsIndex])
        {
            float obs_diff = (*obs)[tsIndex] - Y_obs;
            float sim_diff = (*sim)[tsIndex] - Y_sim;
            sum_obs_sq += obs_diff * obs_diff;
            sum_sim_sq += sim_diff * sim_diff;
            sum_cov += obs_diff * sim_diff;
        }
    }

    // Compute final metrics
    float denom = sqrtf(sum_obs_sq) * sqrtf(sum_sim_sq);
    float r = (denom != 0.0f) ? (sum_cov / denom) : 0.0f;
    float sigma_o = sqrtf(sum_obs_sq / (validQs - 1));
    float sigma_s = sqrtf(sum_sim_sq / (validQs - 1));
    float beta = (Y_obs != 0.0f) ? (Y_sim / Y_obs) : 0.0f;
    float gamma = (Y_sim != 0.0f && Y_obs != 0.0f && sigma_o != 0.0f) ? ((sigma_s / Y_sim) / (sigma_o / Y_obs)) : 0.0f;
    float mkge = 1 - sqrtf((r - 1) * (r - 1) + (beta - 1) * (beta - 1) + (gamma - 1) * (gamma - 1));

    // Handle NaN or infinite case
    if (std::isnan(mkge) || !std::isfinite(mkge))
    {
        return -10000000000.0f;
    }

    return mkge;
}
