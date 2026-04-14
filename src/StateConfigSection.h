#ifndef CONFIG_STATE_SECTION_H
#define CONFIG_STATE_SECTION_H

#include "ConfigSection.h"
#include "Defines.h"
#include "TimeUnit.h"
#include <cstddef>

enum STATE_FILE_FORMAT {
  STATE_FORMAT_GEOTIFF,
  STATE_FORMAT_NETCDF,
  STATE_FORMAT_ASCII,
  STATE_FORMAT_QTY
};

class StateConfigSection : public ConfigSection {

public:
  StateConfigSection();
  ~StateConfigSection();

  static bool ParseStateFileFormat(const char *value,
                                   STATE_FILE_FORMAT *formatOut);
  static bool ParseStateSaveInterval(char *value, TimeUnit *intervalOut);
  static bool ParseInitStateTimestep(const char *value, char *normalizedOut,
                                     size_t normalizedLen);

  CONFIG_SEC_RET ProcessKeyValue(char *name, char *value);
  CONFIG_SEC_RET ValidateSection();

  STATE_FILE_FORMAT GetStateFileFormat();
  TimeUnit *GetStateSaveInterval();
  char *GetInitStateTimestep();
  bool UseStates();

private:
  bool stateFormatSet, stateSaveIntervalSet, initStateTimestepSet;
  STATE_FILE_FORMAT stateFileFormat;
  TimeUnit stateSaveInterval;
  char initStateTimestep[CONFIG_MAX_LEN];
};

extern StateConfigSection *g_stateConfig;

#endif
