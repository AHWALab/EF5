#include "StateTimestepManager.h"
#include "Messages.h"
#include <cstdio>

StateTimestepManager::StateTimestepManager() : initialized(false) {}

StateTimestepManager::~StateTimestepManager() {}

void StateTimestepManager::Initialize(TimeVar *beginTime, TimeUnit *saveInterval, TimeUnit *simTimestep)
{
  // Initialize next save time to the beginning time
  nextSaveTime = *beginTime;
  this->saveInterval = *saveInterval;
  if (this->saveInterval.GetTimeInSec() == 0 && simTimestep)
  {
    this->saveInterval = *simTimestep;
  }
  initialized = true;

  DEBUG_LOGF("State save interval initialized: %ld seconds", this->saveInterval.GetTimeInSec());
}

bool StateTimestepManager::ShouldSaveState(TimeVar *currentTime)
{
  if (!initialized)
  {
    return false;
  }

  // Check if current time has reached or passed the next save time
  if (!(nextSaveTime < *currentTime) && !(nextSaveTime == *currentTime))
  {
    return false;
  }
  if (*currentTime == nextSaveTime || nextSaveTime < *currentTime)
  {
    // Advance next save time
    nextSaveTime.Increment(&this->saveInterval);
    return true;
  }

  return false;
}

long StateTimestepManager::GetTimeUntilNextSave(TimeVar *currentTime)
{
  if (!initialized)
  {
    return -1;
  }

  time_t currentTimeSec = currentTime->currentTimeSec;
  time_t nextSaveTimeSec = nextSaveTime.currentTimeSec;

  if (nextSaveTimeSec > currentTimeSec)
  {
    return (long)(nextSaveTimeSec - currentTimeSec);
  }

  return 0;
}
