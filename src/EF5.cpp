#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "Config.h"
#include "DEMProcessor.h"
#include "Defines.h"
#include "EF5.h"
#include "ExecutionController.h"
#include "RuntimeStats.h"

extern Config *g_config;

void PrintStartupMessage();

int main(int argc, char *argv[])
{

  static ef5::RuntimeStatsReporter ef5_runtime_stats_reporter_instance;

  PrintStartupMessage();

  // ef5 --check [control file]: check parameters and channels, no run
  if (argc >= 2 && strcmp(argv[1], "--check") == 0)
  {
    if (argc > 3)
    {
      printf("Usage: %s --check [control file]\n", argv[0]);
      return 1;
    }
    g_config = new Config((argc == 3) ? argv[2] : "control.txt");
    if (g_config->ParseConfig() != CONFIG_SUCCESS)
    {
      return 1;
    }
    ExecuteChecks();
    return ERROR_SUCCESS;
  }

  if (argc <= 2)
  {

    g_config = new Config((argc == 2) ? argv[1] : "control.txt");
    if (g_config->ParseConfig() != CONFIG_SUCCESS)
    {
      return 1;
    }

    ExecuteTasks();
  }
  else
  {
    int opt = 0;
    int mode = 0;
    char *demFile = NULL, *flowDirFile = NULL, *flowAccFile = NULL;
    while ((opt = getopt(argc, argv, "z:d:a:ps")) != -1)
    {
      switch (opt)
      {
      case 'z':
        demFile = optarg;
        break;
      case 'd':
        flowDirFile = optarg;
        break;
      case 'a':
        flowAccFile = optarg;
        break;
      case 'p':
        mode = 1;
        break;
      case 's':
        mode = 2;
        break;
      }
    }
    ProcessDEM(mode, demFile, flowDirFile, flowAccFile);
  }

  return ERROR_SUCCESS;
}

void PrintStartupMessage()
{
  printf("\033[38;2;255;140;0m");
  printf("********************************************************\n");
  printf("**   Ensemble Framework For Flash Flood Forecasting   **\n");
  printf("**                   Version %s                    **\n",
         EF5_VERSION);
  printf("********************************************************\n");
  printf("\033[0m");
}
