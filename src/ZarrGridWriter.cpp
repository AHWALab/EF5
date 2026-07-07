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
    : initialized(false), nodes(NULL), dataset(NULL), numRows(0), numCols(0),
      noData(-9999.0f) {}

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
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("Zarr");
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
  dataset = driver->CreateMultiDimensional(storePath, NULL, datasetOptions);
  CSLDestroy(datasetOptions);
  if (!dataset) {
    ERROR_LOGF("Failed to create Zarr output store \"%s\".", storePath);
    return false;
  }

  rootGroup = dataset->GetRootGroup();
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

  timeDim =
      rootGroup->CreateDimension("time", "temporal", "increasing", timeSteps);
  yDim = rootGroup->CreateDimension("y", "spatial", "northing",
                                    (GUInt64)numRows);
  xDim = rootGroup->CreateDimension("x", "spatial", "easting",
                                    (GUInt64)numCols);
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
  arrays.clear();
  phaseArray.reset();
  timeArray.reset();
  xDim.reset();
  yDim.reset();
  timeDim.reset();
  rootGroup.reset();
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
  GDALExtendedDataType float64Type = GDALExtendedDataType::Create(GDT_Float64);
  GDALExtendedDataType timeType = GDALExtendedDataType::Create(GDT_Float64);
  GDALExtendedDataType int16Type = GDALExtendedDataType::Create(GDT_Int16);

  timeArray = rootGroup->CreateMDArray("time", {timeDim}, timeType, NULL);
  phaseArray =
      rootGroup->CreateMDArray("forcing_phase", {timeDim}, int16Type, NULL);
  std::shared_ptr<GDALMDArray> yArray =
      rootGroup->CreateMDArray("y", {yDim}, float64Type, NULL);
  std::shared_ptr<GDALMDArray> xArray =
      rootGroup->CreateMDArray("x", {xDim}, float64Type, NULL);

  if (!timeArray || !phaseArray || !yArray || !xArray) {
    ERROR_LOG("Failed to create Zarr coordinate arrays.");
    return false;
  }

  std::shared_ptr<GDALAttribute> timeUnits = timeArray->CreateAttribute(
      "units", {}, GDALExtendedDataType::CreateString(), NULL);
  std::shared_ptr<GDALAttribute> timeDescription = timeArray->CreateAttribute(
      "description", {}, GDALExtendedDataType::CreateString(), NULL);
  if (timeUnits) {
    timeUnits->Write("seconds since 1970-01-01 00:00:00 UTC");
  }
  if (timeDescription) {
    timeDescription->Write("EF5 output timestep timestamp");
  }

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
      !timeArray->Write(start1D, timeCount, NULL, NULL, timeType,
                        timeVals.data())) {
    ERROR_LOG("Failed to initialize Zarr time coordinate array.");
    return false;
  }
  if (timeSteps > 0 &&
      !phaseArray->Write(start1D, timeCount, NULL, NULL, int16Type,
                         phaseValues.data())) {
    ERROR_LOG("Failed to initialize Zarr forcing_phase coordinate array.");
    return false;
  }
  if (!xArray->Write(start1D, xCount, NULL, NULL, float64Type, xVals.data()) ||
      !yArray->Write(start1D, yCount, NULL, NULL, float64Type, yVals.data())) {
    ERROR_LOG("Failed to write Zarr spatial coordinate arrays.");
    return false;
  }

  xArray->SetUnit("degrees_east");
  yArray->SetUnit("degrees_north");
  if (spatialRef) {
    xArray->SetSpatialRef(spatialRef.get());
    yArray->SetSpatialRef(spatialRef.get());
  }

  return true;
}

bool ZarrGridWriter::WritePhaseMetadata() {
  if (!phaseArray) {
    return false;
  }
  std::shared_ptr<GDALAttribute> meanings = phaseArray->CreateAttribute(
      "flag_meanings", {}, GDALExtendedDataType::CreateString(), NULL);
  std::shared_ptr<GDALAttribute> description = phaseArray->CreateAttribute(
      "description", {}, GDALExtendedDataType::CreateString(), NULL);
  if (meanings) {
    meanings->Write("QPE QPF");
  }
  if (description) {
    description->Write("0=QPE timestep, 1=QPF/long-range timestep");
  }
  return true;
}

std::shared_ptr<GDALMDArray>
ZarrGridWriter::GetOrCreateTimeArray(const char *name) {
  std::map<std::string, std::shared_ptr<GDALMDArray>>::iterator itr =
      arrays.find(name);
  if (itr != arrays.end()) {
    return itr->second;
  }

  char **options = NULL;
  options = CSLSetNameValue(options, "COMPRESS", "BLOSC");
  options = CSLSetNameValue(options, "BLOSC_CNAME", "lz4");
  options = CSLSetNameValue(options, "BLOSC_CLEVEL", "5");
  options = CSLSetNameValue(options, "BLOCKSIZE", "1,512,512");
  std::shared_ptr<GDALMDArray> array = rootGroup->CreateMDArray(
      name, {timeDim, yDim, xDim}, GDALExtendedDataType::Create(GDT_Float32),
      options);
  CSLDestroy(options);
  if (!array) {
    ERROR_LOGF("Failed to create Zarr timestep array \"%s\".", name);
    return array;
  }
  array->SetNoDataValue((double)noData);
  if (spatialRef) {
    array->SetSpatialRef(spatialRef.get());
  }
  arrays[name] = array;
  return array;
}

std::shared_ptr<GDALMDArray>
ZarrGridWriter::GetOrCreateStaticArray(const char *name) {
  std::map<std::string, std::shared_ptr<GDALMDArray>>::iterator itr =
      arrays.find(name);
  if (itr != arrays.end()) {
    return itr->second;
  }

  char **options = NULL;
  options = CSLSetNameValue(options, "COMPRESS", "BLOSC");
  options = CSLSetNameValue(options, "BLOSC_CNAME", "lz4");
  options = CSLSetNameValue(options, "BLOSC_CLEVEL", "5");
  options = CSLSetNameValue(options, "BLOCKSIZE", "512,512");
  std::shared_ptr<GDALMDArray> array = rootGroup->CreateMDArray(
      name, {yDim, xDim}, GDALExtendedDataType::Create(GDT_Float32), options);
  CSLDestroy(options);
  if (!array) {
    ERROR_LOGF("Failed to create Zarr static array \"%s\".", name);
    return array;
  }
  array->SetNoDataValue((double)noData);
  if (spatialRef) {
    array->SetSpatialRef(spatialRef.get());
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

  std::shared_ptr<GDALMDArray> array = GetOrCreateTimeArray(name);
  if (!array) {
    return false;
  }

  FillDenseGrid(data);
  GUInt64 start3D[3] = {(GUInt64)timeIndex, 0, 0};
  size_t count3D[3] = {1, (size_t)numRows, (size_t)numCols};
  if (!array->Write(start3D, count3D, NULL, NULL,
                    GDALExtendedDataType::Create(GDT_Float32),
                    denseGrid.data())) {
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
  if (!timeArray->Write(start1D, count1D, NULL, NULL,
                        GDALExtendedDataType::Create(GDT_Float64),
                        &epochSecondsValue) ||
      !phaseArray->Write(start1D, count1D, NULL, NULL,
                         GDALExtendedDataType::Create(GDT_Int16),
                         &forcingPhase)) {
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

  std::shared_ptr<GDALMDArray> array = GetOrCreateStaticArray(name);
  if (!array) {
    return false;
  }

  FillDenseGrid(data);
  GUInt64 start2D[2] = {0, 0};
  size_t count2D[2] = {(size_t)numRows, (size_t)numCols};
  if (!array->Write(start2D, count2D, NULL, NULL,
                    GDALExtendedDataType::Create(GDT_Float32),
                    denseGrid.data())) {
    ERROR_LOGF("Failed to write Zarr static array \"%s\".", name);
    return false;
  }

  return true;
}
