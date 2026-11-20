#ifndef ZARR_GRID_WRITER_H
#define ZARR_GRID_WRITER_H

#include "Grid.h"
#include "GridNode.h"
#ifdef EF5_ZARR
#include "gdal.h"
#endif
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class OGRSpatialReference;

// Writes gridded outputs to one Zarr store (Zarr format v3) through GDAL's
// multidimensional API. Needs GDAL 3.11 or newer: older GDAL versions write a
// draft of Zarr v3 that zarr-python and xarray cannot read. EF5 built without
// it (configure finds no GDAL >= 3.11, or --without-zarr) still has this class,
// but Initialize reports the problem and returns false.
class ZarrGridWriter {
public:
  ZarrGridWriter();
  ~ZarrGridWriter();

  // True if this EF5 was built with Zarr output
  static bool Available() {
#ifdef EF5_ZARR
    return true;
#else
    return false;
#endif
  }

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
#ifdef EF5_ZARR
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
  const char *compression;

  void FillDenseGrid(std::vector<float> *data);
  bool CreateCoordinateArrays(size_t timeSteps);
  GDALMDArrayH GetOrCreateTimeArray(const char *name);
  GDALMDArrayH GetOrCreateStaticArray(const char *name);
  bool WritePhaseMetadata();
  GDALMDArrayH CreateDataArray(const char *name, size_t numDims,
                               GDALDimensionH *dims, const char *blockSize);
#endif
};

#endif
