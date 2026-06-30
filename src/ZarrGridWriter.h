#ifndef ZARR_GRID_WRITER_H
#define ZARR_GRID_WRITER_H

#include "Grid.h"
#include "GridNode.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class GDALDataset;
class GDALGroup;
class GDALMDArray;
class GDALDimension;
class OGRSpatialReference;

class ZarrGridWriter {
public:
  ZarrGridWriter();
  ~ZarrGridWriter();

  bool Initialize(const char *outputPath, std::vector<GridNode> *nodes,
                  size_t timeSteps);
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
  GDALDataset *dataset;
  std::shared_ptr<GDALGroup> rootGroup;
  std::shared_ptr<GDALDimension> timeDim, yDim, xDim;
  std::shared_ptr<GDALMDArray> timeArray, phaseArray;
  std::map<std::string, std::shared_ptr<GDALMDArray>> arrays;
  std::unique_ptr<OGRSpatialReference> spatialRef;
  long numRows, numCols;
  float noData;

  void FillDenseGrid(std::vector<float> *data);
  bool CreateCoordinateArrays(size_t timeSteps);
  std::shared_ptr<GDALMDArray> GetOrCreateTimeArray(const char *name);
  std::shared_ptr<GDALMDArray> GetOrCreateStaticArray(const char *name);
  bool WritePhaseMetadata();
};

#endif
