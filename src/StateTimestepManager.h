#ifndef STATE_TIMESTEP_MANAGER_H
#define STATE_TIMESTEP_MANAGER_H

#include "TimeUnit.h"
#include "TimeVar.h"

class StateTimestepManager {

public:
  StateTimestepManager();
  ~StateTimestepManager();

  // Initialize the manager with the start time and save interval
  void Initialize(TimeVar *beginTime, TimeUnit *saveInterval, TimeUnit *simTimestep);

  // Check if states should be saved at the current time
  bool ShouldSaveState(TimeVar *currentTime);

  // Get the time until next save
  long GetTimeUntilNextSave(TimeVar *currentTime);

private:
  TimeVar nextSaveTime;
  TimeUnit saveInterval;
  bool initialized;
};

#endif
