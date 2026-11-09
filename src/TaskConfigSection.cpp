#include "TaskConfigSection.h"
#include "GriddedOutput.h"
#include "Messages.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm> // For std::fill

std::map<std::string, TaskConfigSection *> g_taskConfigs;

TaskConfigSection::TaskConfigSection(const char *nameVal)
    : styleSet(false), modelSet(false), basinSet(false), precipSet(false), qpfSet(false), petSet(false), outputSet(false),
      stateSet(false), paramsSet(false), timestepSet(false), timeStateSet(false), timeBeginSet(false),
      timeWarmEndSet(false), timeEndSet(false), caliParamSet(false), routingParamsSet(false),
      routingCaliParamSet(false), defaultParamsSet(false), routeSet(false),
      snowParamsSet(false), snowCaliParamSet(false), snowSet(false), tempSet(false), tempFSet(false),
      inundationParamsSet(false), inundationCaliParamSet(false), inundationSet(false),
      timeBeginLRSet(false), timestepLRSet(false),
      model(MODEL_QTY), routing(ROUTE_QTY), snow(SNOW_QTY), inundation(INUNDATION_QTY),
      basin(nullptr), precip(nullptr), qpf(nullptr), pet(nullptr), temp(nullptr), tempf(nullptr),
      params(nullptr), caliParam(nullptr), paramsRouting(nullptr), caliParamRouting(nullptr),
      paramsSnow(nullptr), caliParamSnow(nullptr), paramsInundation(nullptr), caliParamInundation(nullptr),
      defaultGauge(nullptr),
      timeStep(), timeStepLR(), style(STYLE_QTY), timeBegin(), timeWarmEnd(), timeEnd(), timeState(), timeBeginLR(),
      griddedOutputs(OG_NONE), outputType(OUTPUT_TYPE_GEOTIFF)
{
  strcpy(name, nameVal);
  std::fill(output, output + CONFIG_MAX_LEN, 0);
  std::fill(state, state + CONFIG_MAX_LEN, 0);
  std::fill(stdGrid, stdGrid + CONFIG_MAX_LEN, 0);
  std::fill(avgGrid, avgGrid + CONFIG_MAX_LEN, 0);
  std::fill(scGrid, scGrid + CONFIG_MAX_LEN, 0);
  std::fill(actionGrid, actionGrid + CONFIG_MAX_LEN, 0);
  std::fill(minorGrid, minorGrid + CONFIG_MAX_LEN, 0);
  std::fill(moderateGrid, moderateGrid + CONFIG_MAX_LEN, 0);
  std::fill(majorGrid, majorGrid + CONFIG_MAX_LEN, 0);
  std::fill(actionSDGrid, actionSDGrid + CONFIG_MAX_LEN, 0);
  std::fill(minorSDGrid, minorSDGrid + CONFIG_MAX_LEN, 0);
  std::fill(moderateSDGrid, moderateSDGrid + CONFIG_MAX_LEN, 0);
  std::fill(majorSDGrid, majorSDGrid + CONFIG_MAX_LEN, 0);
  std::fill(preloadFile, preloadFile + CONFIG_MAX_LEN, 0);
  std::fill(daFile, daFile + CONFIG_MAX_LEN, 0);
  std::fill(coFile, coFile + CONFIG_MAX_LEN, 0);
  std::fill(basinAvgInput, basinAvgInput + CONFIG_MAX_LEN, 0);
}

TaskConfigSection::~TaskConfigSection() {}

char *TaskConfigSection::GetName() { return name; }

char *TaskConfigSection::GetOutput() { return output; }

char *TaskConfigSection::GetState() { return state; }

char *TaskConfigSection::GetStdGrid() { return stdGrid; }

char *TaskConfigSection::GetAvgGrid() { return avgGrid; }

char *TaskConfigSection::GetScGrid() { return scGrid; }

char *TaskConfigSection::GetActionGrid() { return actionGrid; }

char *TaskConfigSection::GetMinorGrid() { return minorGrid; }

char *TaskConfigSection::GetModerateGrid() { return moderateGrid; }

char *TaskConfigSection::GetMajorGrid() { return majorGrid; }

char *TaskConfigSection::GetActionSDGrid() { return actionSDGrid; }

char *TaskConfigSection::GetMinorSDGrid() { return minorSDGrid; }

char *TaskConfigSection::GetModerateSDGrid() { return moderateSDGrid; }

char *TaskConfigSection::GetMajorSDGrid() { return majorSDGrid; }

char *TaskConfigSection::GetPreloadForcings() { return preloadFile; }

char *TaskConfigSection::GetDAFile() { return daFile; }

char *TaskConfigSection::GetCOFile() { return coFile; }

char *TaskConfigSection::GetBasinAvgInput() { return basinAvgInput; }

TimeVar *TaskConfigSection::GetTimeBegin() { return &timeBegin; }

TimeVar *TaskConfigSection::GetTimeState() { return &timeState; }

TimeVar *TaskConfigSection::GetTimeWarmEnd() { return &timeWarmEnd; }

TimeVar *TaskConfigSection::GetTimeEnd() { return &timeEnd; }

TimeVar *TaskConfigSection::GetTimeBeginLR()
{
  if (!timestepLRSet || !timeBeginLRSet)
  {
    return NULL;
  }
  return &timeBeginLR;
}

TimeUnit *TaskConfigSection::GetTimeStep() { return &timeStep; }

TimeUnit *TaskConfigSection::GetTimeStepLR()
{
  if (!timestepLRSet || !timeBeginLRSet)
  {
    return NULL;
  }
  return &timeStepLR;
}

PrecipConfigSection *TaskConfigSection::GetPrecipSec() { return precip; }

PrecipConfigSection *TaskConfigSection::GetQPFSec() { return qpf; }

PETConfigSection *TaskConfigSection::GetPETSec() { return pet; }

TempConfigSection *TaskConfigSection::GetTempSec() { return temp; }

TempConfigSection *TaskConfigSection::GetTempFSec() { return tempf; }

BasinConfigSection *TaskConfigSection::GetBasinSec() { return basin; }

ParamSetConfigSection *TaskConfigSection::GetParamsSec() { return params; }

CaliParamConfigSection *TaskConfigSection::GetCaliParamSec()
{
  return caliParam;
}

RoutingParamSetConfigSection *TaskConfigSection::GetRoutingParamsSec()
{
  return paramsRouting;
}

RoutingCaliParamConfigSection *TaskConfigSection::GetRoutingCaliParamSec()
{
  return caliParamRouting;
}

SnowParamSetConfigSection *TaskConfigSection::GetSnowParamsSec()
{
  return paramsSnow;
}

SnowCaliParamConfigSection *TaskConfigSection::GetSnowCaliParamSec()
{
  return caliParamSnow;
}

InundationParamSetConfigSection *TaskConfigSection::GetInundationParamsSec()
{
  return paramsInundation;
}

InundationCaliParamConfigSection *
TaskConfigSection::GetInundationCaliParamSec()
{
  return caliParamInundation;
}

RUNSTYLE TaskConfigSection::GetRunStyle() { return style; }

MODELS TaskConfigSection::GetModel() { return model; }

ROUTES TaskConfigSection::GetRouting() { return routing; }

SNOWS TaskConfigSection::GetSnow() { return snow; }

INUNDATIONS TaskConfigSection::GetInundation() { return inundation; }

GaugeConfigSection *TaskConfigSection::GetDefaultGauge()
{
  return defaultGauge;
}

// Helpers for ProcessKeyValue, one per kind of setting. Each logs the same
// messages the settings always had.

// value is one of strings[0..qty-1] (any case): stores its index in out.
template <class E>
static CONFIG_SEC_RET SetOption(const char *value, const char **strings,
                                int qty, E *out, bool *set, const char *what,
                                const char *validWhat, const char *valid)
{
  for (int i = 0; i < qty; i++)
  {
    if (!strcasecmp(value, strings[i]))
    {
      *set = true;
      *out = (E)i;
      return VALID_RESULT;
    }
  }
  ERROR_LOGF("Unknown %s option \"%s\"!", what, value);
  INFO_LOGF("Valid %s options are \"%s\"", validWhat, valid);
  return INVALID_RESULT;
}

// value names a section in sections (any case): stores it in out.
template <class T>
static CONFIG_SEC_RET SetSection(char *value,
                                 std::map<std::string, T *> *sections,
                                 T **out, bool *set, const char *what)
{
  TOLOWER(value);
  typename std::map<std::string, T *>::iterator itr = sections->find(value);
  if (itr == sections->end())
  {
    ERROR_LOGF("Unknown %s \"%s\"!", what, value);
    return INVALID_RESULT;
  }
  *out = itr->second;
  *set = true;
  return VALID_RESULT;
}

// A parameter or calibration set is looked up among the sets of the model
// chosen by an earlier setting (MODEL, ROUTING, SNOW or INUNDATION).
static bool RequireEarlier(bool earlierSet, const char *earlier,
                           const char *key)
{
  if (!earlierSet)
  {
    ERROR_LOGF("The %s setting must be specified before the %s setting in the "
               "task!",
               earlier, key);
  }
  return earlierSet;
}

// Routing sets: those of the chosen routing, and for KW_PARALLEL also those
// of KW.
template <class T>
static CONFIG_SEC_RET SetRoutingSection(char *value,
                                        std::map<std::string, T *> *sets,
                                        ROUTES routing, T **out, bool *set,
                                        const char *what)
{
  TOLOWER(value);
  std::map<std::string, T *> *routingSets = &sets[routing];
  typename std::map<std::string, T *>::iterator itr = routingSets->find(value);
  if (itr == routingSets->end() && routing == ROUTE_KINEMATIC_PARALLEL)
  {
    routingSets = &sets[ROUTE_KINEMATIC];
    itr = routingSets->find(value);
  }
  if (itr == routingSets->end())
  {
    ERROR_LOGF("Unknown %s \"%s\"!", what, value);
    return INVALID_RESULT;
  }
  *out = itr->second;
  *set = true;
  return VALID_RESULT;
}

static CONFIG_SEC_RET SetTime(char *value, TimeVar *out, bool *set,
                              const char *what)
{
  if (!out->LoadTime(value))
  {
    ERROR_LOGF("Unknown %s option \"%s\"", what, value);
    return INVALID_RESULT;
  }
  *set = true;
  return VALID_RESULT;
}

static CONFIG_SEC_RET SetTimeStep(char *value, TimeUnit *out, bool *set,
                                  const char *what)
{
  if (out->ParseUnit(value) == TIME_UNIT_QTY)
  {
    ERROR_LOGF("Unknown %s option \"%s\"", what, value);
    return INVALID_RESULT;
  }
  *set = true;
  return VALID_RESULT;
}

CONFIG_SEC_RET TaskConfigSection::ProcessKeyValue(char *name, char *value)
{
  // Settings that are a file or folder name, copied as given
  struct PathKey
  {
    const char *key;
    char *dest;
    bool *set;
  };
  const PathKey pathKeys[] = {
      {"output", output, &outputSet},
      {"states", state, &stateSet},
      {"preload_file", preloadFile, NULL},
      {"da_file", daFile, NULL},
      {"co_file", coFile, NULL},
      {"rp_stdgrid", stdGrid, NULL},
      {"rp_avggrid", avgGrid, NULL},
      {"rp_csgrid", scGrid, NULL},
      {"action_grid", actionGrid, NULL},
      {"minor_grid", minorGrid, NULL},
      {"moderate_grid", moderateGrid, NULL},
      {"major_grid", majorGrid, NULL},
      {"action_sd_grid", actionSDGrid, NULL},
      {"minor_sd_grid", minorSDGrid, NULL},
      {"moderate_sd_grid", moderateSDGrid, NULL},
      {"major_sd_grid", majorSDGrid, NULL},
  };
  for (size_t i = 0; i < sizeof(pathKeys) / sizeof(pathKeys[0]); i++)
  {
    if (!strcasecmp(name, pathKeys[i].key))
    {
      strcpy(pathKeys[i].dest, value);
      if (pathKeys[i].set)
      {
        *pathKeys[i].set = true;
      }
      return VALID_RESULT;
    }
  }

  // Dates and times
  struct TimeKey
  {
    const char *key;
    TimeVar *dest;
    bool *set;
    const char *what;
  };
  const TimeKey timeKeys[] = {
      {"time_state", &timeState, &timeStateSet, "time state"},
      {"time_begin", &timeBegin, &timeBeginSet, "time begin"},
      {"time_begin_lr", &timeBeginLR, &timeBeginLRSet,
       "time begin long range"},
      {"time_warmend", &timeWarmEnd, &timeWarmEndSet, "time warm end"},
      {"time_end", &timeEnd, &timeEndSet, "time end"},
  };
  for (size_t i = 0; i < sizeof(timeKeys) / sizeof(timeKeys[0]); i++)
  {
    if (!strcasecmp(name, timeKeys[i].key))
    {
      return SetTime(value, timeKeys[i].dest, timeKeys[i].set,
                     timeKeys[i].what);
    }
  }

  if (!strcasecmp(name, "timestep"))
  {
    return SetTimeStep(value, &timeStep, &timestepSet, "timestep");
  }
  if (!strcasecmp(name, "timestep_lr"))
  {
    return SetTimeStep(value, &timeStepLR, &timestepLRSet,
                       "timestep long range");
  }

  // Model choices
  if (!strcasecmp(name, "style"))
  {
    return SetOption(value, runStyleStrings, STYLE_QTY, &style, &styleSet,
                     "run style", "run style",
                     "SIMU, SIMU_RP, CALI_ARS, CALI_DREAM, CLIP_BASIN, "
                     "CLIP_GAUGE, BASIN_AVG");
  }
  if (!strcasecmp(name, "model"))
  {
    return SetOption(value, modelStrings, MODEL_QTY, &model, &modelSet,
                     "water balance model", "water balance", "CREST, SAC, HP");
  }
  if (!strcasecmp(name, "routing"))
  {
    return SetOption(value, routeStrings, ROUTE_QTY, &routing, &routeSet,
                     "routing", "routing", "LR, KW, KW_PARALLEL");
  }
  if (!strcasecmp(name, "snow"))
  {
    return SetOption(value, snowStrings, SNOW_QTY, &snow, &snowSet, "snow",
                     "snow", "SNOW17");
  }
  if (!strcasecmp(name, "inundation"))
  {
    return SetOption(value, inundationStrings, INUNDATION_QTY, &inundation,
                     &inundationSet, "inundation", "inundation",
                     "SIMPLEINUNDATION, VCINUNDATION");
  }

  // Other sections of the control file, by name
  if (!strcasecmp(name, "basin"))
  {
    return SetSection(value, &g_basinConfigs, &basin, &basinSet, "basin");
  }
  if (!strcasecmp(name, "defaultparamsgauge"))
  {
    return SetSection(value, &g_gaugeConfigs, &defaultGauge, &defaultParamsSet,
                      "default parameter gauge");
  }
  if (!strcasecmp(name, "precip"))
  {
    return SetSection(value, &g_precipConfigs, &precip, &precipSet,
                      "precip setting");
  }
  if (!strcasecmp(name, "precipforecast"))
  {
    return SetSection(value, &g_precipConfigs, &qpf, &qpfSet,
                      "precip forecast setting");
  }
  if (!strcasecmp(name, "pet"))
  {
    return SetSection(value, &g_petConfigs, &pet, &petSet, "PET setting");
  }
  if (!strcasecmp(name, "temp"))
  {
    return SetSection(value, &g_tempConfigs, &temp, &tempSet, "temp setting");
  }
  if (!strcasecmp(name, "tempforecast"))
  {
    return SetSection(value, &g_tempConfigs, &tempf, &tempFSet,
                      "temp forecast setting");
  }

  // Parameter and calibration sets of the chosen models
  if (!strcasecmp(name, "param_set"))
  {
    if (!RequireEarlier(modelSet, "MODEL", "PARAM_SET"))
    {
      return INVALID_RESULT;
    }
    return SetSection(value, &g_paramSetConfigs[model], &params, &paramsSet,
                      "parameter set");
  }
  if (!strcasecmp(name, "routing_param_set"))
  {
    if (!RequireEarlier(routeSet, "ROUTING", "ROUTING_PARAM_SET"))
    {
      return INVALID_RESULT;
    }
    return SetRoutingSection(value, g_routingParamSetConfigs, routing,
                             &paramsRouting, &routingParamsSet,
                             "routing parameter set");
  }
  if (!strcasecmp(name, "snow_param_set"))
  {
    if (!RequireEarlier(snowSet, "SNOW", "SNOW_PARAM_SET"))
    {
      return INVALID_RESULT;
    }
    return SetSection(value, &g_snowParamSetConfigs[snow], &paramsSnow,
                      &snowParamsSet, "snow parameter set");
  }
  if (!strcasecmp(name, "inundation_param_set"))
  {
    if (!RequireEarlier(inundationSet, "INUNDATION", "INUNDATION_PARAM_SET"))
    {
      return INVALID_RESULT;
    }
    return SetSection(value, &g_inundationParamSetConfigs[inundation],
                      &paramsInundation, &inundationParamsSet,
                      "inundation parameter set");
  }
  if (!strcasecmp(name, "cali_param"))
  {
    if (!RequireEarlier(modelSet, "MODEL", "CALI_PARAM"))
    {
      return INVALID_RESULT;
    }
    return SetSection(value, &g_caliParamConfigs[model], &caliParam,
                      &caliParamSet, "calibration parameter set");
  }
  if (!strcasecmp(name, "routing_cali_param"))
  {
    if (!RequireEarlier(routeSet, "ROUTING", "ROUTING_CALI_PARAM"))
    {
      return INVALID_RESULT;
    }
    return SetRoutingSection(value, g_routingCaliParamConfigs, routing,
                             &caliParamRouting, &routingCaliParamSet,
                             "routing calibration parameter set");
  }
  if (!strcasecmp(name, "snow_cali_param"))
  {
    if (!RequireEarlier(snowSet, "SNOW", "SNOW_CALI_PARAM"))
    {
      return INVALID_RESULT;
    }
    return SetSection(value, &g_snowCaliParamConfigs[snow], &caliParamSnow,
                      &snowCaliParamSet, "snow calibration parameter set");
  }
  if (!strcasecmp(name, "inundation_cali_param"))
  {
    if (!RequireEarlier(inundationSet, "INUNDATION", "INUNDATION_CALI_PARAM"))
    {
      return INVALID_RESULT;
    }
    return SetSection(value, &g_inundationCaliParamConfigs[inundation],
                      &caliParamInundation, &inundationCaliParamSet,
                      "inundation calibration parameter set");
  }

  // Outputs
  if (!strcasecmp(name, "output_type"))
  {
    if (!strcasecmp(value, "geotiff"))
    {
      outputType = OUTPUT_TYPE_GEOTIFF;
    }
    else if (!strcasecmp(value, "zarr"))
    {
      outputType = OUTPUT_TYPE_ZARR;
    }
    else
    {
      ERROR_LOGF("Unknown output type \"%s\". Valid output types are GEOTIFF and ZARR.", value);
      return INVALID_RESULT;
    }
    return VALID_RESULT;
  }
  if (!strcasecmp(name, "output_grids"))
  {
    return LoadGriddedOutputs(value) ? VALID_RESULT : INVALID_RESULT;
  }
  if (!strcasecmp(name, "basin_avg_input"))
  {
    strncpy(basinAvgInput, value, CONFIG_MAX_LEN - 1);
    basinAvgInput[CONFIG_MAX_LEN - 1] = '\0';
    return VALID_RESULT;
  }

  ERROR_LOGF("Unknown task configuration option \"%s\"", name);
  return INVALID_RESULT;
}

bool TaskConfigSection::LoadGriddedOutputs(char *value)
{

  char *part = strtok(value, "|");
  while (part != NULL)
  {
    bool matchFound = false;
    for (int i = 0; i < OG_QTY; i++)
    {
      if (!strcasecmp(part, GriddedOutputText[i]))
      {
        griddedOutputs |= GriddedOutputFlags[i];
        matchFound = true;
        break;
      }
    }
    if (!matchFound)
    {
      ERROR_LOGF("Unknown output grids flag \"%s\"", part);
      return false;
    }
    part = strtok(NULL, "|");
  }

  return true;
}

CONFIG_SEC_RET TaskConfigSection::ValidateSection()
{
  if (!styleSet)
  {
    ERROR_LOG("The run style was not specified");
    return INVALID_RESULT;
  }
  else if (!modelSet)
  {
    ERROR_LOG("The water balance model was not specified");
    return INVALID_RESULT;
  }
  else if (!basinSet)
  {
    ERROR_LOG("The basin was not specified");
    return INVALID_RESULT;
  }
  else if (!precipSet)
  {
    ERROR_LOG("The precip was not specified");
    return INVALID_RESULT;
  }
  else if (!petSet)
  {
    ERROR_LOG("The pet was not specified");
    return INVALID_RESULT;
  }
  else if (!outputSet)
  {
    ERROR_LOG("The output directory was not specified");
    return INVALID_RESULT;
  }
  else if (!paramsSet)
  {
    ERROR_LOG("The water balance parameter set was not specified");
    return INVALID_RESULT;
  }
  else if (routeSet && !routingParamsSet)
  {
    ERROR_LOG("The routing parameter set was not specified");
    return INVALID_RESULT;
  }
  else if (!timestepSet)
  {
    ERROR_LOG("The timestep was not specified");
    return INVALID_RESULT;
  }
  else if (!timeBeginSet)
  {
    ERROR_LOG("The beginning time was not specified");
    return INVALID_RESULT;
  }
  else if (!timeEndSet)
  {
    ERROR_LOG("The ending time was not specified");
    return INVALID_RESULT;
  }
  else if (snowSet && !snowParamsSet)
  {
    ERROR_LOG("The snow parameter set was not specified");
    return INVALID_RESULT;
  }
  else if (snowSet && !tempSet)
  {
    ERROR_LOG("The temperature forcing was not specified");
    return INVALID_RESULT;
  }
  else if (inundationSet && !inundationParamsSet)
  {
    ERROR_LOG("The inundation parameter set was not specified");
    return INVALID_RESULT;
  }

  if (IsCalibrationRunStyle(style))
  {
    if (!caliParamSet)
    {
      ERROR_LOG(
          "The calibration water balance parameter set was not specified");
      return INVALID_RESULT;
    }

    if (!routingCaliParamSet)
    {
      ERROR_LOG("The calibration routing parameter set was not specified");
      return INVALID_RESULT;
    }

    if (snowSet && !snowCaliParamSet)
    {
      ERROR_LOG("The calibration snow parameter set was not specified");
      return INVALID_RESULT;
    }

    char *gaugeWB = caliParam->GetGauge()->GetName();
    char *gaugeR = caliParamRouting->GetGauge()->GetName();
    char *gaugeS = NULL;
    if (snowSet)
    {
      gaugeS = caliParamSnow->GetGauge()->GetName();
    }
    bool foundWB = false, foundR = false, foundS = false;
    std::vector<GaugeConfigSection *> *bGauges = basin->GetGauges();
    for (size_t i = 0; i < bGauges->size(); i++)
    {
      if (!strcasecmp(gaugeWB, bGauges->at(i)->GetName()))
      {
        foundWB = true;
      }
      if (!strcasecmp(gaugeR, bGauges->at(i)->GetName()))
      {
        foundR = true;
      }
      if (snowSet && !strcasecmp(gaugeS, bGauges->at(i)->GetName()))
      {
        foundS = true;
      }
    }

    if (!foundWB)
    {
      ERROR_LOG("The calibration gauge (water balance cali param) was not "
                "found in the basin!");
      return INVALID_RESULT;
    }
    if (!foundR)
    {
      ERROR_LOG("The calibration gauge (routing cali param) was not found in "
                "the basin!");
      return INVALID_RESULT;
    }
    if (snowSet && !foundS)
    {
      ERROR_LOG("The calibration gauge (snow cali param) was not found in the "
                "basin!");
      return INVALID_RESULT;
    }
  }

  if (!timeWarmEndSet)
  {
    timeWarmEnd = timeBegin;
  }

  return VALID_RESULT;
}

bool TaskConfigSection::IsDuplicate(char *name)
{
  std::map<std::string, TaskConfigSection *>::iterator itr =
      g_taskConfigs.find(name);
  if (itr == g_taskConfigs.end())
  {
    return false;
  }
  else
  {
    return true;
  }
}
