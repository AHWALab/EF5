#ifndef NETCDF_STATE_WRITER_H
#define NETCDF_STATE_WRITER_H

#ifdef __cplusplus
extern "C" {
#endif
#include <netcdf.h>
#ifdef __cplusplus
}
#endif

#include "GridNode.h"
#include <ctime>
#include <vector>

class NetCDFStateWriter {

public:
  NetCDFStateWriter();
  ~NetCDFStateWriter();

  int AppendStateGrid(const char *filePath, const char *varName,
                      const std::vector<GridNode> *nodes,
                      const std::vector<float> *values, time_t epochSeconds);

  int ReadStateGrid(const char *filePath, const char *varName,
                    const std::vector<GridNode> *nodes,
                    std::vector<float> *values, time_t epochSeconds);

  // Returns first/last epoch timestamps stored in the file's time dimension.
  // Returns 0 on success, -1 if the file doesn't exist or has no time records.
  int GetTimeRange(const char *filePath, time_t *firstEpoch, time_t *lastEpoch);

  // Creates dstPath containing only time-steps with epoch <= cutoffEpoch.
  // All state variables are copied for retained time-steps.
  // Returns 0 on success, -1 on error (lastError is set).
  int RebuildUpToCutoff(const char *srcPath, const char *dstPath,
                        time_t cutoffEpoch);

  const char *GetLastError() const;

private:
  int EnsureFileAndCoreVariables(const char *filePath, int *ncid, bool *created);
  int EnsureStateVariable(int ncid, const char *varName, int *varId);
  int GetOrAppendTimeIndex(int ncid, time_t epochSeconds, size_t *timeIndex);
  int GetExactTimeIndex(int ncid, time_t epochSeconds, size_t *timeIndex);
  int BuildDenseGrid(const std::vector<GridNode> *nodes,
                    const std::vector<float> *values,
                    std::vector<float> *denseOut) const;
  int ExtractFromDenseGrid(const std::vector<GridNode> *nodes,
                           const std::vector<float> *denseIn,
                           std::vector<float> *valuesOut) const;

  mutable char lastError[256];
};

#endif
