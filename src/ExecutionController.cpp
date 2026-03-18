#include "ExecutionController.h"
#include "ARS.h"
#include "BasicConfigSection.h"
#include "BasicGrids.h"
#include "BasinConfigSection.h"
#include "DREAM.h"
#include "EnsTaskConfigSection.h"
#include "EnsembleLog.h"
#include "CSVParamSweep.h"
#include "ExecuteConfigSection.h"
#include "GaugeConfigSection.h"
#include "GeographicProjection.h"
#include "LAEAProjection.h"
#include "Messages.h"
#include "Model.h"
#include "Simulator.h"
#include "TaskConfigSection.h"
#include "TimeVar.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#if _OPENMP
#include <omp.h>
#endif

// Global ensemble mode state
bool             g_ensembleMode      = false;
thread_local int g_ensembleTaskIndex = -1;

static void LoadProjection();
static void ExecuteSimulation(TaskConfigSection* task);
static void ExecuteSimulationRP(TaskConfigSection* task);
static void ExecuteCalibrationARS(TaskConfigSection* task);
static void ExecuteCalibrationDREAM(TaskConfigSection* task);
static void ExecuteCalibrationDREAMEns(EnsTaskConfigSection* task);
static void ExecuteSimulationEns(EnsTaskConfigSection* task);
static void ExecuteClipBasin(TaskConfigSection* task);
static void ExecuteClipGauge(TaskConfigSection* task);
static void ExecuteMakeBasic(TaskConfigSection* task);
static void ExecuteMakeBasinAvg(TaskConfigSection* task);
static void ExecuteParamSweep(TaskConfigSection* task);

// Helper function to check if a directory exists and is writable
static bool IsDirWritable(const char* path) {
  struct stat sb;
  if (stat(path, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
    // if the path or the directory does not exist, return false
    return false;
  }
  return (access(path, W_OK) == 0);
}

void ExecuteTasks() {
  if (!g_executeConfig) {
    ERROR_LOGF("%s", "No execute section specified!");
    return;
  }

  std::vector<TaskConfigSection*>*          tasks = g_executeConfig->GetTasks();
  std::vector<TaskConfigSection*>::iterator taskItr;

  std::vector<EnsTaskConfigSection*>*          ensTasks = g_executeConfig->GetEnsTasks();
  std::vector<EnsTaskConfigSection*>::iterator ensTaskItr;

  if (!LoadBasicGrids()) {
    return;
  }
  LoadProjection();

  // Loop through all the ensemble tasks and execute them first
  for (ensTaskItr = ensTasks->begin(); ensTaskItr != ensTasks->end(); ensTaskItr++) {
    EnsTaskConfigSection* ensTask = (*ensTaskItr);
    INFO_LOGF("Executing ensemble task %s", ensTask->GetName());

    switch (ensTask->GetRunStyle()) {
      case STYLE_CALI_DREAM:
        ExecuteCalibrationDREAMEns(ensTask);
        break;
      case STYLE_SIMU:
      case STYLE_SIMU_RP:
        ExecuteSimulationEns(ensTask);
        break;
      default:
        ERROR_LOGF("Unsupport ensemble task run style \"%u\"", ensTask->GetRunStyle());
        break;
    }
  }

  // Loop through all of the tasks and execute them
  for (taskItr = tasks->begin(); taskItr != tasks->end(); taskItr++) {
    TaskConfigSection* task   = (*taskItr);
    const char*        outDir = task->GetOutput();
    if (!IsDirWritable(outDir)) {
      ERROR_LOGF("Output directory '%s' does not exist or is not writable. Aborting task %s.",
                 outDir, task->GetName());
      continue;
    }
    INFO_LOGF("Executing task %s", task->GetName());

    switch (task->GetRunStyle()) {
      case STYLE_SIMU:
        if (task->HasParamSweep()) {
          ExecuteParamSweep(task);
        } else {
          ExecuteSimulation(task);
        }
        break;
      case STYLE_SIMU_RP:
        if (task->HasParamSweep()) {
          ExecuteParamSweep(task);
        } else {
          ExecuteSimulationRP(task);
        }
        break;
      case STYLE_CALI_ARS:
        ExecuteCalibrationARS(task);
        break;
      case STYLE_CALI_DREAM:
        ExecuteCalibrationDREAM(task);
        break;
      case STYLE_CLIP_BASIN:
        ExecuteClipBasin(task);
        break;
      case STYLE_CLIP_GAUGE:
        ExecuteClipGauge(task);
        break;
      case STYLE_MAKE_BASIC:
        ExecuteMakeBasic(task);
        break;
      case STYLE_BASIN_AVG:
        ExecuteMakeBasinAvg(task);
        break;
      default:
        ERROR_LOGF("Unimplemented simulation run style \"%u\"", task->GetRunStyle());
        break;
    }
  }

  FreeBasicGridsData();
}

void LoadProjection() {
  switch (g_basicConfig->GetProjection()) {
    case PROJECTION_GEOGRAPHIC:
      g_Projection = new GeographicProjection();
      g_Projection->SetCellSize(g_DEM->cellSize);
      break;
    case PROJECTION_LAEA:
      g_Projection = new LAEAProjection();
      g_Projection->SetCellSize(g_DEM->cellSize);
      break;
  }

  // Reproject the gauges into the proper map coordinates
  for (std::map<std::string, GaugeConfigSection*>::iterator itr = g_gaugeConfigs.begin();
       itr != g_gaugeConfigs.end(); itr++) {
    GaugeConfigSection* gauge = itr->second;
    float               newX, newY;
    g_Projection->ReprojectPoint(gauge->GetLon(), gauge->GetLat(), &newX, &newY);
    gauge->SetLon(newX);
    gauge->SetLat(newY);
  }
}

void ExecuteSimulation(TaskConfigSection* task) {
  Simulator sim;

  if (sim.Initialize(task)) {
    sim.Simulate();
    sim.CleanUp();
  }
}

void ExecuteSimulationRP(TaskConfigSection* task) {
  Simulator sim;

  sim.Initialize(task);
  sim.Simulate(true);
  sim.CleanUp();
}

void ExecuteCalibrationARS(TaskConfigSection* task) {
  Simulator sim;
  char      buffer[CONFIG_MAX_LEN * 2];

  sim.Initialize(task);
  sprintf(buffer, "%s/%s", task->GetOutput(), "califorcings.bin");
  sim.PreloadForcings(buffer, true);

  printf("Precip loaded!\n");

  ARS ars;
  ars.Initialize(task->GetCaliParamSec(), task->GetRoutingCaliParamSec(), NULL,
                 numModelParams[task->GetModel()], numRouteParams[task->GetRouting()], 0, &sim);
  ars.CalibrateParams();

  sprintf(buffer, "%s/cali_ars.%s.%s.csv", task->GetOutput(),
          task->GetCaliParamSec()->GetGauge()->GetName(), modelStrings[task->GetModel()]);
  ars.WriteOutput(buffer, task->GetModel(), task->GetRouting());
}

void ExecuteCalibrationDREAM(TaskConfigSection* task) {
  Simulator sim;
  char      buffer[CONFIG_MAX_LEN * 2];

  sim.Initialize(task);
  sprintf(buffer, "%s/%s", task->GetOutput(), "califorcings.bin");
  sim.PreloadForcings(buffer, true);

  INFO_LOGF("%s", "Precip loaded!");

  DREAM dream;
  int   numSnow = 0;
  if (task->GetSnow() != SNOW_QTY) {
    numSnow = numSnowParams[task->GetSnow()];
  }
  dream.Initialize(task->GetCaliParamSec(), task->GetRoutingCaliParamSec(),
                   task->GetSnowCaliParamSec(), numModelParams[task->GetModel()],
                   numRouteParams[task->GetRouting()], numSnow, &sim);
  dream.CalibrateParams();

  sprintf(buffer, "%s/cali_dream.%s.%s.csv", task->GetOutput(),
          task->GetCaliParamSec()->GetGauge()->GetName(), modelStrings[task->GetModel()]);
  dream.WriteOutput(buffer, task->GetModel(), task->GetRouting(), task->GetSnow());
}

void ExecuteCalibrationDREAMEns(EnsTaskConfigSection* task) {
  char                             buffer[CONFIG_MAX_LEN * 2];
  std::vector<TaskConfigSection*>* tasks      = task->GetTasks();
  int                              numMembers = (int)tasks->size();
  int                              numParams  = 0;

  std::vector<Simulator> sims;
  std::vector<int>       paramsPerSim;
  paramsPerSim.resize(numMembers);
  sims.resize(numMembers);

  for (int i = 0; i < numMembers; i++) {
    Simulator*         sim      = &(sims[i]);
    TaskConfigSection* thisTask = tasks->at(i);
    sim->Initialize(thisTask);
    sprintf(buffer, "%s/%s", thisTask->GetOutput(), "califorcings.bin");
    sim->PreloadForcings(buffer, true);
    numParams += numModelParams[thisTask->GetModel()];
    paramsPerSim[i] = numModelParams[thisTask->GetModel()];
  }

  float* minParams  = new float[numParams];
  float* maxParams  = new float[numParams];
  int    paramIndex = 0;

  for (int i = 0; i < numMembers; i++) {
    TaskConfigSection* thisTask = tasks->at(i);
    int                cParams  = numModelParams[thisTask->GetModel()];
    memcpy(&(minParams[paramIndex]), thisTask->GetCaliParamSec()->GetParamMins(),
           sizeof(float) * cParams);
    memcpy(&(maxParams[paramIndex]), thisTask->GetCaliParamSec()->GetParamMaxs(),
           sizeof(float) * cParams);
    paramIndex += cParams;
  }

  INFO_LOGF("%s", "Precip loaded!\n");

  DREAM dream;
  dream.Initialize(tasks->at(0)->GetCaliParamSec(), numParams, minParams, maxParams, &sims,
                   &paramsPerSim);
  dream.CalibrateParams();

  sprintf(buffer, "%s/cali_dream.%s.%s.csv", tasks->at(0)->GetOutput(),
          tasks->at(0)->GetCaliParamSec()->GetGauge()->GetName(), "ensemble");
  dream.WriteOutput(buffer, tasks->at(0)->GetModel(), tasks->at(0)->GetRouting(),
                    tasks->at(0)->GetSnow());
}

// ─────────────────────────────────────────────────────────────────────────────
// ExecuteSimulationEns — Run multiple simulation tasks in parallel
// ─────────────────────────────────────────────────────────────────────────────
void ExecuteSimulationEns(EnsTaskConfigSection* ensTask) {
  std::vector<TaskConfigSection*>* tasks      = ensTask->GetTasks();
  int                              numMembers = (int)tasks->size();

  if (numMembers == 0) {
    ERROR_LOGF("%s", "Ensemble task has no member tasks!");
    return;
  }

  EnsembleLogger& logger = EnsembleLogger::Instance();
  logger.Initialize(numMembers);

  // ── Print banner ──────────────────────────────────────────────────────────
  logger.PrintBanner(numMembers);

  // ── Phase 1: Sequential initialization ────────────────────────────────────
  // CarveBasin is expensive. Since all ensemble tasks share the same basin,
  // we carve ONCE (first task) and share the nodes for subsequent tasks.
  INFO_LOGF("%s", "Initializing ensemble members...");

  // Validate all tasks share the same Basin
  BasinConfigSection* sharedBasin = tasks->at(0)->GetBasinSec();
  for (int i = 1; i < numMembers; i++) {
    if (tasks->at(i)->GetBasinSec() != sharedBasin) {
      ERROR_LOGF("%s",
                 "Ensemble tasks must share the same [Basin]! "
                 "Different basins are not supported in ensemble mode.");
      logger.Shutdown();
      return;
    }
  }

  std::vector<Simulator*> sims(numMembers);
  int                     firstValidIdx = -1;  // Index of first successfully initialized simulator

  for (int i = 0; i < numMembers; i++) {
    TaskConfigSection* t = tasks->at(i);
    logger.SetTaskName(i, t->GetName());

    logger.Log(i, "%s%sInitializing...%s", ENS_DIM, ENS_ITALIC, ENS_RESET);

    // Validate output directory
    const char* outDir = t->GetOutput();
    if (!IsDirWritable(outDir)) {
      logger.Log(i, "%s%s" ENS_CROSS " Output dir '%s' not writable!%s", ENS_BOLD, ENS_FG_RED,
                 outDir, ENS_RESET);
      logger.SetTaskFinished(i, false);
      sims[i] = nullptr;
      continue;
    }

    sims[i]     = new Simulator();
    bool initOk = false;

    if (firstValidIdx < 0) {
      // First task: full initialization with CarveBasin
      initOk = sims[i]->Initialize(t);
      if (initOk) {
        firstValidIdx = i;
        logger.Log(i, "%s" ENS_CHECK " Basin carved%s  (shared with all tasks)", ENS_FG_BGREEN,
                   ENS_RESET);
      }
    } else {
      // Subsequent tasks: reuse carved nodes from first task
      initOk = sims[i]->InitializeShared(t, sims[firstValidIdx]->GetNodes(),
                                         sims[firstValidIdx]->GetGaugeMap());
      if (initOk) {
        logger.Log(i, "%s" ENS_CHECK " Initialized%s  (shared basin, own params)", ENS_FG_BCYAN,
                   ENS_RESET);
      }
    }

    if (!initOk) {
      logger.Log(i, "%s%s" ENS_CROSS " Initialization failed!%s", ENS_BOLD, ENS_FG_RED, ENS_RESET);
      logger.SetTaskFinished(i, false);
      delete sims[i];
      sims[i] = nullptr;
      continue;
    }

    // Count total time steps for progress tracking
    TimeVar   tempTime   = *(t->GetTimeBegin());
    TimeVar   endTime    = *(t->GetTimeEnd());
    TimeUnit* ts         = t->GetTimeStep();
    int       totalSteps = 0;
    for (tempTime.Increment(ts); tempTime <= endTime; tempTime.Increment(ts)) {
      totalSteps++;
    }
    logger.SetTaskTotalSteps(i, totalSteps);

    // Open per-task log file in the output directory
    logger.OpenTaskLogFile(i, outDir);

    logger.Log(i, "%s" ENS_CHECK " Ready%s  (%d timesteps, output: %s)", ENS_FG_BGREEN, ENS_RESET,
               totalSteps, outDir);
  }

  // Print task summary table
  logger.PrintTaskTable();

  // Check if we have any valid tasks
  int validTasks = 0;
  for (int i = 0; i < numMembers; i++) {
    if (sims[i] != nullptr) validTasks++;
  }
  if (validTasks == 0) {
    ERROR_LOGF("%s", "No valid tasks to run in ensemble!");
    logger.Shutdown();
    return;
  }

  // ── Phase 2: Parallel simulation ──────────────────────────────────────────
  g_ensembleMode = true;

  if (tasks->at(0)->HasParamSweep()) {
    if (firstValidIdx >= 0 && sims[firstValidIdx] != nullptr) {
      INFO_LOGF("%s", "Preloading forcings into memory for shared ensemble use...");
      sims[firstValidIdx]->PreloadForcingsMemoryOnly();
      for (int i = 0; i < numMembers; i++) {
        if (i != firstValidIdx && sims[i] != nullptr) {
          sims[i]->ShareForcingsFrom(sims[firstValidIdx]);
        }
      }
    }
  }

  INFO_LOGF("Starting %d simulation(s)...", validTasks);
  printf("\n");

  // Start progress bar thread (only in TTY mode)
  std::atomic<bool> progressDone(false);
  std::thread       progressThread;

  if (logger.IsTTY()) {
    progressThread = std::thread([&logger, &progressDone]() {
      while (!progressDone.load()) {
        logger.DrawProgress();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }
      // One final draw to show 100%
      logger.DrawProgress();
    });
  }

#if _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
  for (int i = 0; i < numMembers; i++) {
    if (sims[i] == nullptr) continue;

    g_ensembleTaskIndex = i;

    logger.SetTaskStartTime(i);

    bool rpMode = (tasks->at(i)->GetRunStyle() == STYLE_SIMU_RP);
    sims[i]->Simulate(rpMode);
    sims[i]->CleanUp();

    logger.SetTaskMissingFiles(i, sims[i]->GetMissingQPE());
    logger.SetTaskFinished(i, true);

    if (!logger.IsTTY()) {
      logger.Log(i, "%s%s" ENS_CHECK " Simulation complete%s", ENS_BOLD, ENS_FG_BGREEN, ENS_RESET);
    }
  }

  // Stop progress bar thread — give it time to draw the final 100% state
  // Sleep briefly so the progress thread can pick up all finished states
  if (logger.IsTTY()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
  progressDone.store(true);
  if (progressThread.joinable()) {
    progressThread.join();
  }
  // One final draw after thread is fully stopped (safety net)
  if (logger.IsTTY()) {
    logger.DrawProgress();
  }

  // ── Phase 3: Cleanup & summary ────────────────────────────────────────────
  g_ensembleMode      = false;
  g_ensembleTaskIndex = -1;

  // Print summary
  logger.PrintSummary();

  // Free simulators
  for (int i = 0; i < numMembers; i++) {
    delete sims[i];
  }

  logger.Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// ExecuteParamSweep — Expand a single Task with PARAM_SWEEP_CSV into an
// ensemble of N simulations, one per CSV row.
//
// This function:
//   1. Parses the CSV file to get N sets of parameters
//   2. For each row, creates a synthetic ParamSetConfigSection with params
//      from the CSV (falling back to the base task's params for unspecified)
//   3. Creates a synthetic TaskConfigSection per row with:
//      - Unique output subdirectory: <base_output>/<run_id>/
//      - Overridden param set from CSV
//   4. Groups all tasks into a synthetic EnsTaskConfigSection
//   5. Feeds it into ExecuteSimulationEns (reuses the entire ensemble pipeline)
//
// Backward compatibility: this is only called when PARAM_SWEEP_CSV is set.
// Without it, the normal single-task codepath runs as before.
// ─────────────────────────────────────────────────────────────────────────────
void ExecuteParamSweep(TaskConfigSection* baseTask) {
  INFO_LOGF("Parameter sweep: parsing CSV \"%s\"", baseTask->GetParamSweepCSV());

  // ── Step 1: Parse CSV ──────────────────────────────────────────────────
  std::vector<ParamSweepRow> sweepRows;
  std::string                csvError;

  MODELS model = baseTask->GetModel();
  ROUTES route = baseTask->GetRouting();

  if (!CSVParamSweep::Parse(baseTask->GetParamSweepCSV(), model, route, sweepRows, csvError)) {
    ERROR_LOGF("Failed to parse sweep CSV: %s", csvError.c_str());
    return;
  }

  int numRows = (int)sweepRows.size();
  INFO_LOGF("Parsed %d parameter sets from CSV", numRows);

  // ── Step 2: Get base parameter values ──────────────────────────────────
  // These are the "default" values — CSV rows override them selectively.
  ParamSetConfigSection*        baseParams      = baseTask->GetParamsSec();
  RoutingParamSetConfigSection* baseRouteParams = baseTask->GetRoutingParamsSec();
  GaugeConfigSection*           defaultGauge    = baseTask->GetDefaultGauge();
  int                           numModelP       = numModelParams[model];
  int                           numRouteP       = numRouteParams[route];

  // Get base parameter values for the default gauge
  float* baseModelVals = nullptr;
  float* baseRouteVals = nullptr;

  if (baseParams && defaultGauge) {
    auto* settings = baseParams->GetParamSettings();
    auto  it       = settings->find(defaultGauge);
    if (it != settings->end()) {
      baseModelVals = it->second;
    }
  }
  if (baseRouteParams && defaultGauge) {
    auto* settings = baseRouteParams->GetParamSettings();
    auto  it       = settings->find(defaultGauge);
    if (it != settings->end()) {
      baseRouteVals = it->second;
    }
  }

  // ── Step 3: Create synthetic tasks ─────────────────────────────────────
  // We'll create temporary in-memory TaskConfigSections, one per CSV row.
  // These are managed by us and freed after the ensemble run.

  std::vector<TaskConfigSection*>            sweepTasks;
  std::vector<ParamSetConfigSection*>        sweepParamSets;
  std::vector<RoutingParamSetConfigSection*> sweepRouteParamSets;

  std::string baseOutput = baseTask->GetOutput();
  // Ensure trailing slash
  if (!baseOutput.empty() && baseOutput.back() != '/') {
    baseOutput += '/';
  }

  for (int i = 0; i < numRows; i++) {
    const ParamSweepRow& row = sweepRows[i];

    // ── Create per-row output directory ────────────────────────────────
    std::string rowOutput = baseOutput + row.runId + "/";
    struct stat sb;
    if (stat(rowOutput.c_str(), &sb) != 0) {
      // Directory doesn't exist — create it
      if (mkdir(rowOutput.c_str(), 0755) != 0) {
        ERROR_LOGF("Cannot create output dir: %s", rowOutput.c_str());
        // Clean up already-created tasks
        for (auto* t : sweepTasks) delete t;
        for (auto* p : sweepParamSets) delete p;
        for (auto* rp : sweepRouteParamSets) delete rp;
        return;
      }
    }

    // ── Create model param set with CSV overrides ──────────────────────
    char paramName[CONFIG_MAX_LEN];
    snprintf(paramName, CONFIG_MAX_LEN, "sweep_%s_model", row.runId.c_str());

    ParamSetConfigSection* psc = new ParamSetConfigSection(paramName, model);

    // Build the param array: start from base, override with CSV
    float* newParams = new float[numModelP];
    for (int p = 0; p < numModelP; p++) {
      if (row.crestSet[p]) {
        newParams[p] = row.crestParams[p];
      } else if (baseModelVals) {
        newParams[p] = baseModelVals[p];
      } else {
        newParams[p] = 0.0f;
      }
    }

    // Insert params for the default gauge
    if (defaultGauge) {
      psc->GetParamSettings()->insert(
          std::pair<GaugeConfigSection*, float*>(defaultGauge, newParams));
    }

    // Copy param grids from base
    if (baseParams) {
      std::vector<std::string>* baseGrids = baseParams->GetParamGrids();
      std::vector<std::string>* newGrids  = psc->GetParamGrids();
      *newGrids                           = *baseGrids;
    }

    sweepParamSets.push_back(psc);

    // ── Create routing param set with CSV overrides ────────────────────
    char routeParamName[CONFIG_MAX_LEN];
    snprintf(routeParamName, CONFIG_MAX_LEN, "sweep_%s_route", row.runId.c_str());

    RoutingParamSetConfigSection* rpsc = new RoutingParamSetConfigSection(routeParamName, route);

    float* newRouteParams = new float[numRouteP];
    for (int p = 0; p < numRouteP; p++) {
      if (row.kwSet[p]) {
        newRouteParams[p] = row.kwParams[p];
      } else if (baseRouteVals) {
        newRouteParams[p] = baseRouteVals[p];
      } else {
        newRouteParams[p] = 0.0f;
      }
    }

    if (defaultGauge) {
      rpsc->GetParamSettings()->insert(
          std::pair<GaugeConfigSection*, float*>(defaultGauge, newRouteParams));
    }

    // Copy route param grids from base
    if (baseRouteParams) {
      std::vector<std::string>* baseGrids = baseRouteParams->GetParamGrids();
      std::vector<std::string>* newGrids  = rpsc->GetParamGrids();
      *newGrids                           = *baseGrids;
    }

    sweepRouteParamSets.push_back(rpsc);

    // ── Create cloned TaskConfigSection with overrides ─────────────────
    // Copy constructor clones everything from baseTask (precip, PET,
    // basin, time, etc.), then we override just name/output/params.
    TaskConfigSection* newTask = new TaskConfigSection(*baseTask);
    newTask->SetName(row.runId.c_str());
    newTask->SetOutput(rowOutput.c_str());
    newTask->SetParamsSec(psc);
    newTask->SetRoutingParamsSec(rpsc);

    sweepTasks.push_back(newTask);
  }

  // ── Step 4: Create synthetic EnsTaskConfigSection ──────────────────────
  EnsTaskConfigSection synthEns("param_sweep");

  // Set the run style
  char ensStyleBuf[32];
  snprintf(ensStyleBuf, sizeof(ensStyleBuf), "%s", runStyleStrings[baseTask->GetRunStyle()]);
  synthEns.ProcessKeyValue((char*)"style", ensStyleBuf);

  // Register sweep tasks in the global config temporarily so EnsTask can
  // look them up by name (EnsTaskConfigSection::ProcessKeyValue does a
  // g_taskConfigs lookup).
  for (auto* t : sweepTasks) {
    char tName[CONFIG_MAX_LEN];
    strcpy(tName, t->GetName());
    TOLOWER(tName);
    g_taskConfigs[tName] = t;
    synthEns.ProcessKeyValue((char*)"task", tName);
  }

  // ── Step 5: Run the ensemble ───────────────────────────────────────────
  ExecuteSimulationEns(&synthEns);

  // ── Step 6: Cleanup ────────────────────────────────────────────────────
  // Remove synthetic entries from global registries
  for (int i = 0; i < numRows; i++) {
    char tName[CONFIG_MAX_LEN];
    strcpy(tName, sweepTasks[i]->GetName());
    TOLOWER(tName);
    g_taskConfigs.erase(tName);
  }

  // Free synthetic objects (note: param arrays inside ParamSetConfigSection
  // are freed by their destructors since they were inserted via GetParamSettings)
  for (auto* t : sweepTasks) delete t;
  for (auto* p : sweepParamSets) delete p;
  for (auto* rp : sweepRouteParamSets) delete rp;
}

void ExecuteClipBasin(TaskConfigSection* task) {
  std::map<GaugeConfigSection*, float*> fullParamSettings, *paramSettings, fullRouteParamSettings,
      *routeParamSettings;
  std::vector<GridNode> nodes;
  GaugeMap              gaugeMap;

  // Get the parameter settings for this task
  paramSettings = task->GetParamsSec()->GetParamSettings();
  float *                                         defaultParams = NULL, *defaultRouteParams = NULL;
  GaugeConfigSection*                             gs   = task->GetDefaultGauge();
  std::map<GaugeConfigSection*, float*>::iterator pitr = paramSettings->find(gs);
  if (pitr != paramSettings->end()) {
    defaultParams = pitr->second;
  }
  routeParamSettings = task->GetRoutingParamsSec()->GetParamSettings();
  pitr               = routeParamSettings->find(gs);
  if (pitr != routeParamSettings->end()) {
    defaultRouteParams = pitr->second;
  }

  CarveBasin(task->GetBasinSec(), &nodes, paramSettings, &fullParamSettings, &gaugeMap,
             defaultParams, routeParamSettings, &fullRouteParamSettings, defaultRouteParams, NULL,
             NULL, NULL, NULL, NULL, NULL);

  ClipBasicGrids(task->GetBasinSec(), &nodes, task->GetBasinSec()->GetName(), task->GetOutput());
}

void ExecuteClipGauge(TaskConfigSection* task) {
  std::map<GaugeConfigSection*, float*> fullParamSettings, *paramSettings, fullRouteParamSettings,
      *routeParamSettings;
  std::vector<GridNode> nodes;
  GaugeMap              gaugeMap;

  // Get the parameter settings for this task
  paramSettings = task->GetParamsSec()->GetParamSettings();
  float *                                         defaultParams = NULL, *defaultRouteParams = NULL;
  GaugeConfigSection*                             gs   = task->GetDefaultGauge();
  std::map<GaugeConfigSection*, float*>::iterator pitr = paramSettings->find(gs);
  if (pitr != paramSettings->end()) {
    defaultParams = pitr->second;
  }
  routeParamSettings = task->GetRoutingParamsSec()->GetParamSettings();
  pitr               = routeParamSettings->find(gs);
  if (pitr != routeParamSettings->end()) {
    defaultRouteParams = pitr->second;
  }

  CarveBasin(task->GetBasinSec(), &nodes, paramSettings, &fullParamSettings, &gaugeMap,
             defaultParams, routeParamSettings, &fullRouteParamSettings, defaultRouteParams, NULL,
             NULL, NULL, NULL, NULL, NULL);

  GridLoc* loc = (*(task->GetBasinSec()->GetGauges()))[0]->GetGridLoc();
  ClipBasicGrids(loc->x, loc->y, 10, task->GetOutput());
}

void ExecuteMakeBasic(TaskConfigSection* task) {
  MakeBasic();
}

void ExecuteMakeBasinAvg(TaskConfigSection* task) {
  Simulator sim;

  if (sim.Initialize(task)) {
    const char* inputDir = task->GetBasinAvgInput();
    if (!inputDir || inputDir[0] == '\0') {
      inputDir = task->GetOutput();
    }
    sim.BasinAvg(inputDir);
    sim.CleanUp();
  }
}
