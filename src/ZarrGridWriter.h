#ifndef ZARR_GRID_WRITER_H
#define ZARR_GRID_WRITER_H

#include "Grid.h"
#include "GridNode.h"
#include "gdal.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class OGRSpatialReference;

class ZarrGridWriter {
public:
  ZarrGridWriter();
  ~ZarrGridWriter();

  bool Initialize(const char *outputPath, const char *storeName,
                  std::vector<GridNode> *nodes, size_t timeSteps);
  bool IsInitialized() const { return initialized; }
  bool WriteTimeGrid(const char *name, size_t timeIndex, int64_t epochSeconds,
                     int16_t forcingPhase, std::vector<float> *data);
  bool WriteTimeMetadata(size_t timeIndex, int64_t epochSeconds,
                         int16_t forcingPhase);
  bool WriteStaticGrid(const char *name, std::vector<float> *data);
  void Close();

private:
  bool initialized;
  std::vector<GridNode> *nodes;
  std::vector<float> denseGrid;
  std::vector<int16_t> phaseValues;
  GDALDatasetH dataset;
  GDALGroupH rootGroup;
  GDALDimensionH timeDim, yDim, xDim;
  GDALMDArrayH timeArray, phaseArray;
  std::map<std::string, GDALMDArrayH> arrays;
  std::unique_ptr<OGRSpatialReference> spatialRef;
  long numRows, numCols;
  float noData;

  void FillDenseGrid(std::vector<float> *data);
  bool CreateCoordinateArrays(size_t timeSteps);
  GDALMDArrayH GetOrCreateTimeArray(const char *name);
  GDALMDArrayH GetOrCreateStaticArray(const char *name);
  bool WritePhaseMetadata();
};

#endif
