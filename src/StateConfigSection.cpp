#include "StateConfigSection.h"
#include "Messages.h"
#include <cstdio>
#include <cstring>

StateConfigSection *g_stateConfig;

StateConfigSection::StateConfigSection()
    : stateFormatSet(false), stateSaveIntervalSet(false), initStateTimestepSet(false),
      stateFileFormat(STATE_FORMAT_GEOTIFF)
{
  initStateTimestep[0] = 0; // Initialize as empty string
}

StateConfigSection::~StateConfigSection() {}

bool StateConfigSection::ParseStateFileFormat(const char *value,
                                              STATE_FILE_FORMAT *formatOut)
{
  if (!strcasecmp(value, "geotiff"))
  {
    *formatOut = STATE_FORMAT_GEOTIFF;
    return true;
  }
  if (!strcasecmp(value, "netcdf"))
  {
    *formatOut = STATE_FORMAT_NETCDF;
    return true;
  }
  if (!strcasecmp(value, "ascii"))
  {
    *formatOut = STATE_FORMAT_ASCII;
    return true;
  }
  return false;
}

bool StateConfigSection::ParseStateSaveInterval(char *value,
                                                TimeUnit *intervalOut)
{
  return intervalOut->ParseUnit(value) != TIME_UNIT_QTY;
}

bool StateConfigSection::ParseInitStateTimestep(const char *value,
                                                char *normalizedOut,
                                                size_t normalizedLen)
{
  int y = 0, mon = 0, d = 0, h = 0, m = 0;
  if (sscanf(value, "%4d%2d%2d_%2d%2d", &y, &mon, &d, &h, &m) != 5)
  {
    if (sscanf(value, "%4d%2d%2d%2d%2d", &y, &mon, &d, &h, &m) != 5)
    {
      return false;
    }
  }

  snprintf(normalizedOut, normalizedLen, "%04d%02d%02d_%02d%02d", y, mon, d, h,
           m);
  return true;
}

STATE_FILE_FORMAT StateConfigSection::GetStateFileFormat() { return stateFileFormat; }

TimeUnit *StateConfigSection::GetStateSaveInterval() { return &stateSaveInterval; }

char *StateConfigSection::GetInitStateTimestep() { return initStateTimestep; }

bool StateConfigSection::UseStates() { return stateFormatSet; }

CONFIG_SEC_RET StateConfigSection::ProcessKeyValue(char *name, char *value)
{

  if (!strcasecmp(name, "statefileformat"))
  {
    if (ParseStateFileFormat(value, &stateFileFormat))
    {
      stateFormatSet = true;
    }
    else
    {
      ERROR_LOGF("Unknown state file format option \"%s\"", value);
      INFO_LOGF("Valid state file format options are \"%s\"", "GEOTIFF, NETCDF, ASCII");
      return INVALID_RESULT;
    }
  }
  else if (!strcasecmp(name, "statesaveinterval"))
  {
    if (!ParseStateSaveInterval(value, &stateSaveInterval))
    {
      ERROR_LOGF("Unknown state save interval option \"%s\"", value);
      return INVALID_RESULT;
    }
    stateSaveIntervalSet = true;
  }
  else if (!strcasecmp(name, "initstatetimestep"))
  {
    if (!ParseInitStateTimestep(value, initStateTimestep,
                                sizeof(initStateTimestep)))
    {
      ERROR_LOGF("Invalid init state timestep option \"%s\"", value);
      INFO_LOGF("Expected format is \"%s\"", "YYYYMMDD_HHMM");
      return INVALID_RESULT;
    }
    initStateTimestepSet = true;
  }
  else
  {
    return INVALID_RESULT;
  }

  return VALID_RESULT;
}

CONFIG_SEC_RET StateConfigSection::ValidateSection()
{
  // State section is optional - no validation errors required
  return VALID_RESULT;
}
