#include <cstdio>
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
  // ── Modern professional banner with refreshing teal & gold palette ──
  printf("\n");
  printf("\033[38;2;0;206;209m");  // Teal border
  printf("  ╔══════════════════════════════════════════════════════╗\n");
  printf("  ║  \033[38;2;127;255;212m▐▛▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▌\033[38;2;0;206;209m  ║\n");
  printf("  ║  \033[38;2;127;255;212m▐▌  \033[38;2;255;255;255mEnsemble Framework for Flash Flood Forecasting\033[38;2;127;255;212m  ▐▌\033[38;2;0;206;209m  ║\n");
  printf("  ║  \033[38;2;127;255;212m▐▌  \033[38;2;255;215;0m◆  Version %-42s ◆\033[38;2;127;255;212m  ▐▌\033[38;2;0;206;209m  ║\n",
         EF5_VERSION);
  printf("  ║  \033[38;2;127;255;212m▐▙▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▟\033[38;2;0;206;209m  ║\n");
  printf("  ╚══════════════════════════════════════════════════════╝\n");
  printf("\033[0m\n");
}
