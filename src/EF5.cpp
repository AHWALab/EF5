#include <cstdio>
#include <unistd.h>

#include "Config.h"
#include "DEMProcessor.h"
#include "Defines.h"
#include "EF5.h"
#include "ExecutionController.h"
#include "RuntimeStats.h"

extern Config* g_config;

void PrintStartupMessage();

int main(int argc, char* argv[]) {
  static ef5::RuntimeStatsReporter ef5_runtime_stats_reporter_instance;

  PrintStartupMessage();

  if (argc <= 2) {
    g_config = new Config((argc == 2) ? argv[1] : "control.txt");
    if (g_config->ParseConfig() != CONFIG_SUCCESS) {
      return 1;
    }

    ExecuteTasks();
  } else {
    int   opt     = 0;
    int   mode    = 0;
    char *demFile = NULL, *flowDirFile = NULL, *flowAccFile = NULL;
    while ((opt = getopt(argc, argv, "z:d:a:ps")) != -1) {
      switch (opt) {
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

void PrintStartupMessage() {
  // True-color Bright Cyan for the text
  printf("\033[38;2;0;225;255m");
  printf(" ███████╗███████╗███████╗    ██╗   ██╗██████╗       ██████╗ \n");
  printf(" ██╔════╝██╔════╝██╔════╝    ██║   ██║╚════██╗     ██╔═████╗\n");
  printf(" █████╗  █████╗  ███████╗    ██║   ██║ █████╔╝     ██║██╔██║\n");
  printf(" ██╔══╝  ██╔══╝  ╚════██║    ╚██╗ ██╔╝██╔═══╝      ████╔╝██║\n");
  printf(" ███████╗██║     ███████║     ╚████╔╝ ███████╗ ██╗ ╚██████╔╝\n");
  printf(" ╚══════╝╚═╝     ╚══════╝      ╚═══╝  ╚══════╝ ╚═╝  ╚═════╝ \n\n");

  // True-color Deep Blue for the border
  printf("\033[38;2;0;102;204m");
  printf(" ========================================================== \n");

  // White text for the framework description
  printf("\033[38;2;255;255;255m");
  printf(" |      Ensemble Framework For Flash Flood Forecasting      | \n");
  printf(" |                      Version %-6s                      | \n", EF5_VERSION);

  // Deep Blue for the bottom border
  printf("\033[38;2;0;102;204m");
  printf(" ========================================================== \n");

  // Reset colors
  printf("\033[0m");
}
