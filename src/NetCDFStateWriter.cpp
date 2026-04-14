#include "NetCDFStateWriter.h"
#include "BasicGrids.h"
#include "Messages.h"
#include <cmath>
#include <cstdio>
#include <cstring>

NetCDFStateWriter::NetCDFStateWriter()
{
  lastError[0] = 0;
}

NetCDFStateWriter::~NetCDFStateWriter() {}

const char *NetCDFStateWriter::GetLastError() const { return lastError; }

int NetCDFStateWriter::EnsureFileAndCoreVariables(const char *filePath, int *ncid,
                                                  bool *created)
{
  *created = false;
  int ret = nc_open(filePath, NC_WRITE, ncid);
  if (ret == NC_NOERR)
  {
    return 0;
  }

  ret = nc_create(filePath, NC_CLOBBER, ncid);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to create netCDF file %s: %s", filePath,
             nc_strerror(ret));
    return -1;
  }

  *created = true;

  int dimTime = -1, dimY = -1, dimX = -1;
  ret = nc_def_dim(*ncid, "time", NC_UNLIMITED, &dimTime);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to define time dimension: %s", nc_strerror(ret));
    return -1;
  }
  ret = nc_def_dim(*ncid, "y", (size_t)g_DEM->numRows, &dimY);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to define y dimension: %s", nc_strerror(ret));
    return -1;
  }
  ret = nc_def_dim(*ncid, "x", (size_t)g_DEM->numCols, &dimX);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to define x dimension: %s", nc_strerror(ret));
    return -1;
  }

  int varTime = -1;
  ret = nc_def_var(*ncid, "time", NC_DOUBLE, 1, &dimTime, &varTime);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to define time variable: %s", nc_strerror(ret));
    return -1;
  }
  nc_put_att_text(*ncid, varTime, "units", strlen("seconds since 1970-01-01 00:00:00"),
                  "seconds since 1970-01-01 00:00:00");
  nc_put_att_text(*ncid, varTime, "calendar", strlen("standard"), "standard");

  nc_put_att_text(*ncid, NC_GLOBAL, "Conventions", strlen("CF-1.8"), "CF-1.8");
  nc_put_att_text(*ncid, NC_GLOBAL, "title", strlen("EF5 model states"), "EF5 model states");

  ret = nc_enddef(*ncid);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to finalize netCDF definition: %s", nc_strerror(ret));
    return -1;
  }

  return 0;
}

int NetCDFStateWriter::EnsureStateVariable(int ncid, const char *varName, int *varId)
{
  int ret = nc_inq_varid(ncid, varName, varId);
  if (ret == NC_NOERR)
  {
    return 0;
  }
  if (ret != NC_ENOTVAR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to query variable %s: %s", varName,
             nc_strerror(ret));
    return -1;
  }

  int dimTime = -1, dimY = -1, dimX = -1;
  if (nc_inq_dimid(ncid, "time", &dimTime) != NC_NOERR ||
      nc_inq_dimid(ncid, "y", &dimY) != NC_NOERR ||
      nc_inq_dimid(ncid, "x", &dimX) != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Missing one or more core dimensions");
    return -1;
  }

  int dims[3] = {dimTime, dimY, dimX};
  ret = nc_redef(ncid);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to enter define mode: %s", nc_strerror(ret));
    return -1;
  }

  ret = nc_def_var(ncid, varName, NC_FLOAT, 3, dims, varId);
  if (ret != NC_NOERR)
  {
    nc_enddef(ncid);
    snprintf(lastError, sizeof(lastError), "Failed to define state variable %s: %s", varName,
             nc_strerror(ret));
    return -1;
  }
  nc_put_att_text(ncid, *varId, "coordinates", strlen("time y x"), "time y x");

  ret = nc_enddef(ncid);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to leave define mode: %s", nc_strerror(ret));
    return -1;
  }

  return 0;
}

int NetCDFStateWriter::GetOrAppendTimeIndex(int ncid, time_t epochSeconds,
                                            size_t *timeIndex)
{
  int varTime = -1;
  int ret = nc_inq_varid(ncid, "time", &varTime);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Missing time variable: %s", nc_strerror(ret));
    return -1;
  }

  int dimTime = -1;
  ret = nc_inq_dimid(ncid, "time", &dimTime);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Missing time dimension: %s", nc_strerror(ret));
    return -1;
  }

  size_t nTime = 0;
  ret = nc_inq_dimlen(ncid, dimTime, &nTime);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to query time length: %s", nc_strerror(ret));
    return -1;
  }

  double target = (double)epochSeconds;
  if (nTime > 0)
  {
    std::vector<double> tvals(nTime);
    ret = nc_get_var_double(ncid, varTime, tvals.data());
    if (ret != NC_NOERR)
    {
      snprintf(lastError, sizeof(lastError), "Failed to read time values: %s", nc_strerror(ret));
      return -1;
    }
    for (size_t i = 0; i < nTime; i++)
    {
      if (fabs(tvals[i] - target) <= 0.5)
      {
        *timeIndex = i;
        return 0;
      }
    }
  }

  *timeIndex = nTime;
  size_t start = *timeIndex;
  ret = nc_put_var1_double(ncid, varTime, &start, &target);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to append time value: %s", nc_strerror(ret));
    return -1;
  }

  return 0;
}

int NetCDFStateWriter::GetExactTimeIndex(int ncid, time_t epochSeconds,
                                         size_t *timeIndex)
{
  int varTime = -1;
  int ret = nc_inq_varid(ncid, "time", &varTime);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Missing time variable: %s", nc_strerror(ret));
    return -1;
  }

  int dimTime = -1;
  ret = nc_inq_dimid(ncid, "time", &dimTime);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Missing time dimension: %s", nc_strerror(ret));
    return -1;
  }

  size_t nTime = 0;
  ret = nc_inq_dimlen(ncid, dimTime, &nTime);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to query time length: %s", nc_strerror(ret));
    return -1;
  }
  if (nTime == 0)
  {
    snprintf(lastError, sizeof(lastError), "State file has no time records");
    return -1;
  }

  std::vector<double> tvals(nTime);
  ret = nc_get_var_double(ncid, varTime, tvals.data());
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed to read time values: %s", nc_strerror(ret));
    return -1;
  }

  double target = (double)epochSeconds;
  for (size_t i = 0; i < nTime; i++)
  {
    if (fabs(tvals[i] - target) <= 0.5)
    {
      *timeIndex = i;
      return 0;
    }
  }

  snprintf(lastError, sizeof(lastError), "Requested state time not found in netCDF file");
  return -1;
}

int NetCDFStateWriter::BuildDenseGrid(const std::vector<GridNode> *nodes,
                                      const std::vector<float> *values,
                                      std::vector<float> *denseOut) const
{
  if (!g_DEM)
  {
    snprintf(lastError, sizeof(lastError), "DEM is not initialized");
    return -1;
  }
  if (nodes->size() != values->size())
  {
    snprintf(lastError, sizeof(lastError), "Node/value size mismatch for state write");
    return -1;
  }

  const size_t total = (size_t)g_DEM->numRows * (size_t)g_DEM->numCols;
  denseOut->assign(total, NAN);
  for (size_t i = 0; i < nodes->size(); i++)
  {
    const GridNode &n = nodes->at(i);
    if (n.x < 0 || n.y < 0 || n.x >= g_DEM->numCols || n.y >= g_DEM->numRows)
    {
      continue;
    }
    const size_t idx = (size_t)n.y * (size_t)g_DEM->numCols + (size_t)n.x;
    denseOut->at(idx) = values->at(i);
  }
  return 0;
}

int NetCDFStateWriter::ExtractFromDenseGrid(const std::vector<GridNode> *nodes,
                                            const std::vector<float> *denseIn,
                                            std::vector<float> *valuesOut) const
{
  if (!g_DEM)
  {
    snprintf(lastError, sizeof(lastError), "DEM is not initialized");
    return -1;
  }

  valuesOut->resize(nodes->size());
  for (size_t i = 0; i < nodes->size(); i++)
  {
    const GridNode &n = nodes->at(i);
    if (n.x < 0 || n.y < 0 || n.x >= g_DEM->numCols || n.y >= g_DEM->numRows)
    {
      valuesOut->at(i) = 0.0f;
      continue;
    }
    const size_t idx = (size_t)n.y * (size_t)g_DEM->numCols + (size_t)n.x;
    valuesOut->at(i) = denseIn->at(idx);
  }
  return 0;
}

int NetCDFStateWriter::AppendStateGrid(const char *filePath, const char *varName,
                                       const std::vector<GridNode> *nodes,
                                       const std::vector<float> *values,
                                       time_t epochSeconds)
{
  int ncid = -1;
  bool created = false;
  if (EnsureFileAndCoreVariables(filePath, &ncid, &created) != 0)
  {
    ERROR_LOGF("%s", lastError);
    return -1;
  }

  int varId = -1;
  if (EnsureStateVariable(ncid, varName, &varId) != 0)
  {
    ERROR_LOGF("%s", lastError);
    nc_close(ncid);
    return -1;
  }

  size_t timeIndex = 0;
  if (GetOrAppendTimeIndex(ncid, epochSeconds, &timeIndex) != 0)
  {
    ERROR_LOGF("%s", lastError);
    nc_close(ncid);
    return -1;
  }

  std::vector<float> dense;
  if (BuildDenseGrid(nodes, values, &dense) != 0)
  {
    ERROR_LOGF("%s", lastError);
    nc_close(ncid);
    return -1;
  }

  size_t start[3] = {timeIndex, 0, 0};
  size_t count[3] = {1, (size_t)g_DEM->numRows, (size_t)g_DEM->numCols};
  int ret = nc_put_vara_float(ncid, varId, start, count, dense.data());
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed writing state variable %s: %s", varName,
             nc_strerror(ret));
    ERROR_LOGF("%s", lastError);
    nc_close(ncid);
    return -1;
  }

  nc_close(ncid);
  return 0;
}

int NetCDFStateWriter::ReadStateGrid(const char *filePath, const char *varName,
                                     const std::vector<GridNode> *nodes,
                                     std::vector<float> *values,
                                     time_t epochSeconds)
{
  int ncid = -1;
  int ret = nc_open(filePath, NC_NOWRITE, &ncid);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed opening netCDF file %s: %s", filePath,
             nc_strerror(ret));
    ERROR_LOGF("%s", lastError);
    return -1;
  }

  int varId = -1;
  ret = nc_inq_varid(ncid, varName, &varId);
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Variable %s not found in %s", varName, filePath);
    ERROR_LOGF("%s", lastError);
    nc_close(ncid);
    return -1;
  }

  size_t timeIndex = 0;
  if (GetExactTimeIndex(ncid, epochSeconds, &timeIndex) != 0)
  {
    ERROR_LOGF("%s", lastError);
    nc_close(ncid);
    return -1;
  }

  std::vector<float> dense((size_t)g_DEM->numRows * (size_t)g_DEM->numCols);
  size_t start[3] = {timeIndex, 0, 0};
  size_t count[3] = {1, (size_t)g_DEM->numRows, (size_t)g_DEM->numCols};
  ret = nc_get_vara_float(ncid, varId, start, count, dense.data());
  if (ret != NC_NOERR)
  {
    snprintf(lastError, sizeof(lastError), "Failed reading state variable %s: %s", varName,
             nc_strerror(ret));
    ERROR_LOGF("%s", lastError);
    nc_close(ncid);
    return -1;
  }

  nc_close(ncid);
  return ExtractFromDenseGrid(nodes, &dense, values);
}
