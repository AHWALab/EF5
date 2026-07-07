#include "ZarrGridWriter.h"
#include "Messages.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "cpl_vsi.h"
#include "gdal_priv.h"
#include "ogr_spatialref.h"
#include <algorithm>
#include <cstdio>

extern LongGrid *g_DEM;

ZarrGridWriter::ZarrGridWriter()
    : initialized(false), nodes(NULL), dataset(NULL), rootGroup(NULL),
      timeDim(NULL), yDim(NULL), xDim(NULL), timeArray(NULL),
      phaseArray(NULL), numRows(0), numCols(0), noData(-9999.0f) {}

ZarrGridWriter::~ZarrGridWriter() { Close(); }

bool ZarrGridWriter::Initialize(const char *outputPath, const char *storeName,
                                std::vector<GridNode> *nodesVal,
                                size_t timeSteps) {
  Close();

  if (!g_DEM) {
    ERROR_LOG("Cannot initialize Zarr writer without a DEM.");
    return false;
  }

  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName("Zarr");
  if (!driver) {
    ERROR_LOG("GDAL Zarr driver is not available.");
    return false;
  }

  char storePath[CONFIG_MAX_LEN * 2];
  snprintf(storePath, sizeof(storePath), "%s/%s.zarr", outputPath, storeName);

  VSIStatBufL storeStat;
  if (VSIStatL(storePath, &storeStat) == 0) {
    if (VSIRmdirRecursive(storePath) != 0) {
      ERROR_LOGF("Failed to remove existing Zarr output store \"%s\".",
                 storePath);
      return false;
    }
  }

  char **datasetOptions = NULL;
  datasetOptions = CSLSetNameValue(datasetOptions, "FORMAT", "ZARR_V2");
  dataset = GDALCreateMultiDimensional(driver, storePath, NULL, datasetOptions);
  CSLDestroy(datasetOptions);
  if (!dataset) {
    ERROR_LOGF("Failed to create Zarr output store \"%s\".", storePath);
    return false;
  }

  rootGroup = GDALDatasetGetRootGroup(dataset);
  if (!rootGroup) {
    ERROR_LOGF("Failed to access root group for Zarr output store \"%s\".",
               storePath);
    Close();
    return false;
  }

  nodes = nodesVal;
  numRows = g_DEM->numRows;
  numCols = g_DEM->numCols;
  denseGrid.resize((size_t)numRows * (size_t)numCols, noData);
  phaseValues.resize(timeSteps, -1);

  timeDim = GDALGroupCreateDimension(rootGroup, "time", "temporal",
                                     "increasing", (GUInt64)timeSteps, NULL);
  yDim = GDALGroupCreateDimension(rootGroup, "y", "spatial", "northing",
                                  (GUInt64)numRows, NULL);
  xDim = GDALGroupCreateDimension(rootGroup, "x", "spatial", "easting",
                                  (GUInt64)numCols, NULL);
  if (!timeDim || !yDim || !xDim) {
    ERROR_LOGF("Failed to create Zarr dimensions time=%lu y=%ld x=%ld.",
               (unsigned long)timeSteps, numRows, numCols);
    Close();
    return false;
  }

  spatialRef.reset(new OGRSpatialReference());
  if (g_DEM->geoSet && g_DEM->geographicType > 0) {
    if (spatialRef->importFromEPSG(g_DEM->geographicType) != OGRERR_NONE) {
      spatialRef->SetWellKnownGeogCS("WGS84");
    }
  } else {
    spatialRef->SetWellKnownGeogCS("WGS84");
  }

  if (!CreateCoordinateArrays(timeSteps) || !WritePhaseMetadata()) {
    Close();
    return false;
  }

  initialized = true;
  return true;
}

void ZarrGridWriter::Close() {
  for (std::map<std::string, GDALMDArrayH>::iterator itr = arrays.begin();
       itr != arrays.end(); ++itr) {
    if (itr->second) {
      GDALMDArrayRelease(itr->second);
    }
  }
  arrays.clear();
  if (phaseArray) {
    GDALMDArrayRelease(phaseArray);
    phaseArray = NULL;
  }
  if (timeArray) {
    GDALMDArrayRelease(timeArray);
    timeArray = NULL;
  }
  if (xDim) {
    GDALDimensionRelease(xDim);
    xDim = NULL;
  }
  if (yDim) {
    GDALDimensionRelease(yDim);
    yDim = NULL;
  }
  if (timeDim) {
    GDALDimensionRelease(timeDim);
    timeDim = NULL;
  }
  if (rootGroup) {
    GDALGroupRelease(rootGroup);
    rootGroup = NULL;
  }
  spatialRef.reset();
  if (dataset) {
    GDALClose(dataset);
    dataset = NULL;
  }
  denseGrid.clear();
  phaseValues.clear();
  nodes = NULL;
  initialized = false;
}

bool ZarrGridWriter::CreateCoordinateArrays(size_t timeSteps) {
  GDALExtendedDataTypeH float64Type =
      GDALExtendedDataTypeCreate(GDT_Float64);
  GDALExtendedDataTypeH timeType = GDALExtendedDataTypeCreate(GDT_Float64);
  GDALExtendedDataTypeH int16Type = GDALExtendedDataTypeCreate(GDT_Int16);

  GDALDimensionH timeDims[1] = {timeDim};
  GDALDimensionH yDims[1] = {yDim};
  GDALDimensionH xDims[1] = {xDim};
  timeArray =
      GDALGroupCreateMDArray(rootGroup, "time", 1, timeDims, timeType, NULL);
  phaseArray = GDALGroupCreateMDArray(rootGroup, "forcing_phase", 1, timeDims,
                                      int16Type, NULL);
  GDALMDArrayH yArray =
      GDALGroupCreateMDArray(rootGroup, "y", 1, yDims, float64Type, NULL);
  GDALMDArrayH xArray =
      GDALGroupCreateMDArray(rootGroup, "x", 1, xDims, float64Type, NULL);

  if (!timeArray || !phaseArray || !yArray || !xArray) {
    ERROR_LOG("Failed to create Zarr coordinate arrays.");
    if (xArray) {
      GDALMDArrayRelease(xArray);
    }
    if (yArray) {
      GDALMDArrayRelease(yArray);
    }
    GDALExtendedDataTypeRelease(int16Type);
    GDALExtendedDataTypeRelease(timeType);
    GDALExtendedDataTypeRelease(float64Type);
    return false;
  }

  GDALExtendedDataTypeH stringType = GDALExtendedDataTypeCreateString(0);
  GDALAttributeH timeUnits =
      GDALMDArrayCreateAttribute(timeArray, "units", 0, NULL, stringType, NULL);
  GDALAttributeH timeDescription = GDALMDArrayCreateAttribute(
      timeArray, "description", 0, NULL, stringType, NULL);
  if (timeUnits) {
    GDALAttributeWriteString(timeUnits,
                             "seconds since 1970-01-01 00:00:00 UTC");
    GDALAttributeRelease(timeUnits);
  }
  if (timeDescription) {
    GDALAttributeWriteString(timeDescription, "EF5 output timestep timestamp");
    GDALAttributeRelease(timeDescription);
  }
  GDALExtendedDataTypeRelease(stringType);

  std::vector<double> xVals(numCols), yVals(numRows);
  for (long col = 0; col < numCols; col++) {
    xVals[col] = g_DEM->extent.left + ((double)col + 0.5) * g_DEM->cellSize;
  }
  for (long row = 0; row < numRows; row++) {
    yVals[row] = g_DEM->extent.top - ((double)row + 0.5) * g_DEM->cellSize;
  }

  GUInt64 start1D[1] = {0};
  size_t xCount[1] = {(size_t)numCols};
  size_t yCount[1] = {(size_t)numRows};
  size_t timeCount[1] = {timeSteps};

  std::vector<double> timeVals(timeSteps, 0.0);
  if (timeSteps > 0 &&
      !GDALMDArrayWrite(timeArray, start1D, timeCount, NULL, NULL, timeType,
                        timeVals.data(), NULL, 0)) {
    ERROR_LOG("Failed to initialize Zarr time coordinate array.");
    GDALMDArrayRelease(xArray);
    GDALMDArrayRelease(yArray);
    GDALExtendedDataTypeRelease(int16Type);
    GDALExtendedDataTypeRelease(timeType);
    GDALExtendedDataTypeRelease(float64Type);
    return false;
  }
  if (timeSteps > 0 &&
      !GDALMDArrayWrite(phaseArray, start1D, timeCount, NULL, NULL, int16Type,
                        phaseValues.data(), NULL, 0)) {
    ERROR_LOG("Failed to initialize Zarr forcing_phase coordinate array.");
    GDALMDArrayRelease(xArray);
    GDALMDArrayRelease(yArray);
    GDALExtendedDataTypeRelease(int16Type);
    GDALExtendedDataTypeRelease(timeType);
    GDALExtendedDataTypeRelease(float64Type);
    return false;
  }
  if (!GDALMDArrayWrite(xArray, start1D, xCount, NULL, NULL, float64Type,
                        xVals.data(), NULL, 0) ||
      !GDALMDArrayWrite(yArray, start1D, yCount, NULL, NULL, float64Type,
                        yVals.data(), NULL, 0)) {
    ERROR_LOG("Failed to write Zarr spatial coordinate arrays.");
    GDALMDArrayRelease(xArray);
    GDALMDArrayRelease(yArray);
    GDALExtendedDataTypeRelease(int16Type);
    GDALExtendedDataTypeRelease(timeType);
    GDALExtendedDataTypeRelease(float64Type);
    return false;
  }

  GDALMDArraySetUnit(xArray, "degrees_east");
  GDALMDArraySetUnit(yArray, "degrees_north");
  if (spatialRef) {
    OGRSpatialReferenceH srsHandle =
        OGRSpatialReference::ToHandle(spatialRef.get());
    GDALMDArraySetSpatialRef(xArray, srsHandle);
    GDALMDArraySetSpatialRef(yArray, srsHandle);
  }

  GDALMDArrayRelease(xArray);
  GDALMDArrayRelease(yArray);
  GDALExtendedDataTypeRelease(int16Type);
  GDALExtendedDataTypeRelease(timeType);
  GDALExtendedDataTypeRelease(float64Type);
  return true;
}

bool ZarrGridWriter::WritePhaseMetadata() {
  if (!phaseArray) {
    return false;
  }
  GDALExtendedDataTypeH stringType = GDALExtendedDataTypeCreateString(0);
  GDALAttributeH meanings = GDALMDArrayCreateAttribute(
      phaseArray, "flag_meanings", 0, NULL, stringType, NULL);
  GDALAttributeH description = GDALMDArrayCreateAttribute(
      phaseArray, "description", 0, NULL, stringType, NULL);
  if (meanings) {
    GDALAttributeWriteString(meanings, "QPE QPF");
    GDALAttributeRelease(meanings);
  }
  if (description) {
    GDALAttributeWriteString(description,
                             "0=QPE timestep, 1=QPF/long-range timestep");
    GDALAttributeRelease(description);
  }
  GDALExtendedDataTypeRelease(stringType);
  return true;
}

GDALMDArrayH ZarrGridWriter::GetOrCreateTimeArray(const char *name) {
  std::map<std::string, GDALMDArrayH>::iterator itr = arrays.find(name);
  if (itr != arrays.end()) {
    return itr->second;
  }

  char **options = NULL;
  options = CSLSetNameValue(options, "COMPRESS", "BLOSC");
  options = CSLSetNameValue(options, "BLOSC_CNAME", "lz4");
  options = CSLSetNameValue(options, "BLOSC_CLEVEL", "5");
  options = CSLSetNameValue(options, "BLOCKSIZE", "1,512,512");
  GDALDimensionH dims[3] = {timeDim, yDim, xDim};
  GDALExtendedDataTypeH float32Type =
      GDALExtendedDataTypeCreate(GDT_Float32);
  GDALMDArrayH array =
      GDALGroupCreateMDArray(rootGroup, name, 3, dims, float32Type, options);
  GDALExtendedDataTypeRelease(float32Type);
  CSLDestroy(options);
  if (!array) {
    ERROR_LOGF("Failed to create Zarr timestep array \"%s\".", name);
    return array;
  }
  GDALMDArraySetNoDataValueAsDouble(array, (double)noData);
  if (spatialRef) {
    GDALMDArraySetSpatialRef(array,
                             OGRSpatialReference::ToHandle(spatialRef.get()));
  }
  arrays[name] = array;
  return array;
}

GDALMDArrayH ZarrGridWriter::GetOrCreateStaticArray(const char *name) {
  std::map<std::string, GDALMDArrayH>::iterator itr = arrays.find(name);
  if (itr != arrays.end()) {
    return itr->second;
  }

  char **options = NULL;
  options = CSLSetNameValue(options, "COMPRESS", "BLOSC");
  options = CSLSetNameValue(options, "BLOSC_CNAME", "lz4");
  options = CSLSetNameValue(options, "BLOSC_CLEVEL", "5");
  options = CSLSetNameValue(options, "BLOCKSIZE", "512,512");
  GDALDimensionH dims[2] = {yDim, xDim};
  GDALExtendedDataTypeH float32Type =
      GDALExtendedDataTypeCreate(GDT_Float32);
  GDALMDArrayH array =
      GDALGroupCreateMDArray(rootGroup, name, 2, dims, float32Type, options);
  GDALExtendedDataTypeRelease(float32Type);
  CSLDestroy(options);
  if (!array) {
    ERROR_LOGF("Failed to create Zarr static array \"%s\".", name);
    return array;
  }
  GDALMDArraySetNoDataValueAsDouble(array, (double)noData);
  if (spatialRef) {
    GDALMDArraySetSpatialRef(array,
                             OGRSpatialReference::ToHandle(spatialRef.get()));
  }
  arrays[name] = array;
  return array;
}

void ZarrGridWriter::FillDenseGrid(std::vector<float> *data) {
  std::fill(denseGrid.begin(), denseGrid.end(), noData);
  size_t numNodes = nodes->size();
  for (size_t i = 0; i < numNodes; i++) {
    GridNode *node = &(nodes->at(i));
    if (!node->gauge) {
      continue;
    }
    denseGrid[(size_t)node->y * (size_t)numCols + (size_t)node->x] =
        data->at(i);
  }
}

bool ZarrGridWriter::WriteTimeGrid(const char *name, size_t timeIndex,
                                   int64_t epochSeconds, int16_t forcingPhase,
                                   std::vector<float> *data) {
  if (!initialized) {
    ERROR_LOG("Zarr writer was not initialized.");
    return false;
  }
  if (timeIndex >= phaseValues.size()) {
    ERROR_LOGF("Zarr timestep index %lu is outside configured timestep count %lu.",
               (unsigned long)timeIndex, (unsigned long)phaseValues.size());
    return false;
  }

  GDALMDArrayH array = GetOrCreateTimeArray(name);
  if (!array) {
    return false;
  }

  FillDenseGrid(data);
  GUInt64 start3D[3] = {(GUInt64)timeIndex, 0, 0};
  size_t count3D[3] = {1, (size_t)numRows, (size_t)numCols};
  GDALExtendedDataTypeH float32Type =
      GDALExtendedDataTypeCreate(GDT_Float32);
  int writeOk = GDALMDArrayWrite(array, start3D, count3D, NULL, NULL,
                                 float32Type, denseGrid.data(), NULL, 0);
  GDALExtendedDataTypeRelease(float32Type);
  if (!writeOk) {
    ERROR_LOGF("Failed to write Zarr timestep array \"%s\" at index %lu.", name,
               (unsigned long)timeIndex);
    return false;
  }

  return WriteTimeMetadata(timeIndex, epochSeconds, forcingPhase);
}

bool ZarrGridWriter::WriteTimeMetadata(size_t timeIndex, int64_t epochSeconds,
                                       int16_t forcingPhase) {
  if (!initialized) {
    ERROR_LOG("Zarr writer was not initialized.");
    return false;
  }
  if (timeIndex >= phaseValues.size()) {
    ERROR_LOGF("Zarr timestep index %lu is outside configured timestep count %lu.",
               (unsigned long)timeIndex, (unsigned long)phaseValues.size());
    return false;
  }

  GUInt64 start1D[1] = {(GUInt64)timeIndex};
  size_t count1D[1] = {1};
  phaseValues[timeIndex] = forcingPhase;
  double epochSecondsValue = (double)epochSeconds;
  GDALExtendedDataTypeH float64Type =
      GDALExtendedDataTypeCreate(GDT_Float64);
  GDALExtendedDataTypeH int16Type = GDALExtendedDataTypeCreate(GDT_Int16);
  int writeOk =
      GDALMDArrayWrite(timeArray, start1D, count1D, NULL, NULL, float64Type,
                       &epochSecondsValue, NULL, 0) &&
      GDALMDArrayWrite(phaseArray, start1D, count1D, NULL, NULL, int16Type,
                       &forcingPhase, NULL, 0);
  GDALExtendedDataTypeRelease(int16Type);
  GDALExtendedDataTypeRelease(float64Type);
  if (!writeOk) {
    ERROR_LOGF("Failed to write Zarr timestep metadata at index %lu.",
               (unsigned long)timeIndex);
    return false;
  }

  return true;
}

bool ZarrGridWriter::WriteStaticGrid(const char *name,
                                     std::vector<float> *data) {
  if (!initialized) {
    ERROR_LOG("Zarr writer was not initialized.");
    return false;
  }

  GDALMDArrayH array = GetOrCreateStaticArray(name);
  if (!array) {
    return false;
  }

  FillDenseGrid(data);
  GUInt64 start2D[2] = {0, 0};
  size_t count2D[2] = {(size_t)numRows, (size_t)numCols};
  GDALExtendedDataTypeH float32Type =
      GDALExtendedDataTypeCreate(GDT_Float32);
  int writeOk = GDALMDArrayWrite(array, start2D, count2D, NULL, NULL,
                                 float32Type, denseGrid.data(), NULL, 0);
  GDALExtendedDataTypeRelease(float32Type);
  if (!writeOk) {
    ERROR_LOGF("Failed to write Zarr static array \"%s\".", name);
    return false;
  }

  return true;
}
