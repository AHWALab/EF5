#include "DatedName.h"
#include "Messages.h"
#include <cstdio>
#include <cstring>

const char *datedNameStrings[] = {
    "YYYY",
    "MM",
    "DD",
    "HH",
    "UU",
    "SS",
};

const int datePartLen[] = {
    4,
    2,
    2,
    2,
    2,
    2,
};

void DatedName::SetNameStr(const char *nameStr)
{
  strcpy(inUseName, nameStr);
  memset(inUseTimeParts, 0, sizeof(bool) * TIME_UNIT_QTY);
}

bool DatedName::ProcessName(TimeUnit *freq)
{
  resolution = freq->GetTimeUnit();

  // Verify that our name string has all of the necessary variables
  // for the resolution specified by frequency
  for (int i = 0; i <= resolution; i++)
  {
    timeParts[i] = strstr(inUseName, datedNameStrings[i]);
    if (timeParts[i] == NULL)
    {
      ERROR_LOGF("Expected to find \"%s\" in \"%s\" but it was not found!",
                 datedNameStrings[i], inUseName);
      return false;
    }
    inUseTimeParts[i] = true;
  }

  return true;
}

bool DatedName::ProcessNameLoose(TimeUnit *freq)
{
  // resolution = freq->GetTimeUnit();
  resolution = SECONDS;

  // Verify that our name string has all of the necessary variables
  // for the resolution specified by frequency
  for (int i = 0; i <= resolution; i++)
  {
    timeParts[i] = strstr(inUseName, datedNameStrings[i]);
    if (timeParts[i] != NULL)
    {
      inUseTimeParts[i] = true;
    }
  }

  return true;
}

void DatedName::UpdateName(tm *ptm)
{
  char dateParts[6][12];

  // Range checks for all date components
  int year = ptm->tm_year + 1900;
  if (year < 0 || year > 9999)
  {
    ERROR_LOGF("Year %d out of range (0-9999)", year);
    return;
  }
  int month = ptm->tm_mon + 1;
  if (month < 1 || month > 12)
  {
    ERROR_LOGF("Month %d out of range (1-12)", month);
    return;
  }
  int day = ptm->tm_mday;
  if (day < 1 || day > 31)
  {
    ERROR_LOGF("Day %d out of range (1-31)", day);
    return;
  }
  int hour = ptm->tm_hour;
  if (hour < 0 || hour > 23)
  {
    ERROR_LOGF("Hour %d out of range (0-23)", hour);
    return;
  }
  int min = ptm->tm_min;
  if (min < 0 || min > 59)
  {
    ERROR_LOGF("Minute %d out of range (0-59)", min);
    return;
  }
  int sec = ptm->tm_sec;
  if (sec < 0 || sec > 60)
  {
    ERROR_LOGF("Second %d out of range (0-60)", sec);
    return;
  }

  memset(dateParts, 0, 6 * 12);              // Zero out the buffer memory
  snprintf(dateParts[0], 12, "%04d", year);  // "0000" to "9999"
  snprintf(dateParts[1], 12, "%02d", month); // "01" to "12"
  snprintf(dateParts[2], 12, "%02d", day);   // "01" to "31"
  snprintf(dateParts[3], 12, "%02d", hour);  // "00" to "23"
  snprintf(dateParts[4], 12, "%02d", min);   // "00" to "59"
  snprintf(dateParts[5], 12, "%02d", sec);   // "00" to "60"

  for (int i = 0; i <= resolution; i++)
  {
    if (inUseTimeParts[i])
    {
      memcpy(timeParts[i], dateParts[i], datePartLen[i]);
    }
  }
}
