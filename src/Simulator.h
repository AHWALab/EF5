#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "GaugeConfigSection.h"
#include "GaugeMap.h"
#include "GridNode.h"
#include "Model.h"
#include "ModelBase.h"
#include "PETConfigSection.h"
#include "PETReader.h"
#include "PrecipConfigSection.h"
#include "PrecipReader.h"
#include "RPSkewness.h"
#include "TaskConfigSection.h"
#include "TempConfigSection.h"
#include "TempReader.h"
#include "ZarrGridWriter.h"

class Simulator
{
public:
    Simulator() : zarrGridWriter(NULL) {}
    bool Initialize(TaskConfigSection *taskN);
    bool CheckChannels(TaskConfigSection *taskN); // ef5 --check (ChannelCheck.cpp)
    void PreloadForcings(char *file, bool cali);
    bool LoadSavedForcings(char *file, bool cali);
    void SaveForcings(char *file);

    void CleanUp();
    void BasinAvg(const char* inputDir);
    void BasinAvgPrecip();
    void Simulate(bool trackPeaks = false);
    float SimulateForCali(float *testParams);
    float *SimulateForCaliTS(float *testParams);
    float *GetObsTS();
    const std::vector<float>& GetAllScores() const { return lastAllScores; }
    size_t GetNumSteps() { return totalTimeStepsOutsideWarm; }

private:
    bool InitializeBasic(TaskConfigSection *task);
    bool InitializeTimeSteps(TaskConfigSection *task);
    void InitializeForcingFiles(TaskConfigSection *task);
    bool InitializeNodes(TaskConfigSection *task);
    bool CreateModels(TaskConfigSection *task);
    bool InitializeSimu(TaskConfigSection *task);
    bool ReadLP3Grids(TaskConfigSection *task);
    void OpenGaugeOutputs(TaskConfigSection *task);
    void CountTimeSteps();
    bool InitializeCali(TaskConfigSection *task);
    bool InitializeGridParams(TaskConfigSection *task);

    void SimulateDistributed(bool trackPeaks);
    bool PrepareDistributedRun(bool trackPeaks, TempReader *tempReader);
    void WarnParameterProblems(); // ChannelCheck.cpp
    std::vector<float> *RunModelsForStep(float stepHoursReal, size_t tsIndex,
                                         bool outputTS);
    void AddToPrecipTotals(bool qpf, std::vector<float> *currentPrecip,
                           float stepHoursReal);
    void SaveGaugeTimeSeries(size_t tsIndex, std::vector<float> *currentPrecip);
    void UpdateRunningMaxima(bool trackPeaks, int indexYear);
    void SimulateLumped();

    float GetNumSimulatedYears();
    int LoadForcings(PrecipReader *precipReader, PETReader *petReader,
                     TempReader *tempReader);
    void AdvanceForcingTimes(bool withTemp);
    bool ReadTemperature(TempReader *tempReader);
    bool ReadPrecip(PrecipReader *precipReader, bool *readQPF);
    bool ReadPET(PETReader *petReader);
    void SaveLP3Params();
    void SaveTSOutput();
    bool IsOutputTS();
    void LoadDAFile(TaskConfigSection *task);
    void AssimilateData();
    void OutputCombinedOutput();

    bool ReadThresFile(char *file, std::vector<GridNode> *nodes,
                       std::vector<float> *thresVals);
    float ComputeThresValue(float discharge, float action, float minor,
                            float moderate, float major);
    float ComputeThresValueP(float discharge, float action, float actionSD,
                             float minor, float minorSD, float moderate,
                             float moderateSD, float major, float majorSD);
    float CalcProb(float discharge, float mean, float sd);

    // These guys are the basic variables
    TaskConfigSection *task;
    GridNodeVec nodes;
    GridNodeVec lumpedNodes;
    WaterBalanceModel *wbModel;
    RoutingModel *rModel;
    SnowModel *sModel;
    InundationModel *iModel;
    GaugeMap gaugeMap;
    PrecipConfigSection *precipSec, *qpfSec;
    PETConfigSection *petSec;
    TempConfigSection *tempSec, *tempFSec;
    std::vector<GaugeConfigSection *> *gauges;
    std::map<GaugeConfigSection *, float *> fullParamSettings, *paramSettings;
    std::map<GaugeConfigSection *, float *> fullParamSettingsRoute,
        *paramSettingsRoute;
    std::map<GaugeConfigSection *, float *> fullParamSettingsSnow,
        *paramSettingsSnow;
    std::map<GaugeConfigSection *, float *> fullParamSettingsInundation,
        *paramSettingsInundation;
    TimeUnit *timeStep, *timeStepSR, *timeStepLR, *timeStepPrecip, *timeStepQPF,
        *timeStepPET, *timeStepTemp, *timeStepTempF;
    float precipConvert, qpfConvert, petConvert, timeStepHours, timeStepHoursLR;
    TimeVar currentTime, currentTimePrecip, currentTimeQPF, currentTimePET,
        currentTimeTemp, currentTimeTempF, beginTime, endTime, warmEndTime,
        beginLRTime;
    DatedName *precipFile, *qpfFile, *petFile, *tempFile, *tempFFile,
        currentTimeText, currentTimeTextOutput;
    std::vector<float> currentFF, currentSF, currentQ, avgPrecip, avgPET, avgSWE,
        currentSWE, avgT, avgSM, avgFF, avgSF, currentDepth, computeVec;
    std::vector<FloatGrid *> paramGrids, paramGridsRoute, paramGridsSnow,
        paramGridsInundation;
    bool hasQPF, hasTempF, wantsDA;
    bool inLR;
    std::vector<bool> gaugesUsed;

    // This is for simulations only
    std::vector<float> currentPrecipSimu, currentPETSimu, currentTempSimu;
    std::vector<float> currentPrecipSnow; // precip after snowmelt (snow model)
    std::vector<FILE *> gaugeOutputs;
    int griddedOutputs;
    bool outputRP;
    bool useStates, saveStates;
    bool preloadedForcings;
    std::vector<RPData> rpData;
    char *outputPath;
    char *statePath;
    TimeVar stateTime;
    std::vector<std::vector<float>> peakVals;
    GridWriterFull gridWriter;
    ZarrGridWriter *zarrGridWriter;
    float numYears;
    int missingQPE, missingQPF;
    std::vector<float> maxGrid, rpMaxGrid, maxDepthGrid, SM;
    std::vector<float> rpGrid;             // return period of this step
    std::vector<float> qpeAccum, qpfAccum; // precip totals since warm-up
    size_t zarrTimeIndex;                  // next time slot in the Zarr store
    bool zarrHandcatchmentWritten;

    // Flood-threshold grids for the thres, maxthres and maxthresp outputs,
    // one value per node (see ReadThresholdGrids)
    struct ThresholdGrids
    {
        bool loaded;   // action..major were read
        bool loadedSD; // the four *SD grids were read too
        std::vector<float> action, minor, moderate, major;
        std::vector<float> actionSD, minorSD, moderateSD, majorSD;
    };
    ThresholdGrids thresholds;
    void ReadThresholdGrids();

    bool OutputMaxFiles(const char* prefix);
    bool WriteTimeStepOutputs();
    bool WriteEndOfRunOutputs();
    void WriteResultsJson(double runTimeSeconds);
    bool WriteTimeStepGrid(const char *name, const char *modelName,
                           size_t zarrTimeIndex, std::vector<float> *grid);
    bool WriteEndOfRunGrid(const char *prefix, const char *name,
                           std::vector<float> *grid);

    // This is for calibrations only
    std::vector<std::vector<float>> currentPrecipCali, currentPETCali,
        currentTempCali;
    std::vector<float> obsQ, simQ;
    CaliParamConfigSection *caliParamSec;
    RoutingCaliParamConfigSection *routingCaliParamSec;
    SnowCaliParamConfigSection *snowCaliParamSec;
    OBJECTIVES objectiveFunc;
    GaugeConfigSection *caliGauge;
    float *caliWBParams;
    float *caliRParams;
    float *caliSParams;
    std::vector<float> lastAllScores;   // stores all OBJECTIVE_QTY metric scores from last SimulateForCali call
    size_t totalTimeSteps, totalTimeStepsOutsideWarm;
    int numWBParams, numRParams, numSParams;
    int caliGaugeIndex;
    std::vector<WaterBalanceModel *> caliWBModels;
    std::vector<RoutingModel *> caliRModels;
    std::vector<SnowModel *> caliSModels;
    std::vector<float *> caliWBCurrentParams, caliRCurrentParams,
        caliSCurrentParams;
    std::vector<std::map<GaugeConfigSection *, float *>> caliWBFullParamSettings,
        caliRFullParamSettings, caliSFullParamSettings;
};

#endif
