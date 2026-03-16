#ifndef MESSAGES_H
#define MESSAGES_H

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

#ifndef _WIN32

#include "EnsembleLog.h"

// ── Ensemble-aware logging ──────────────────────────────────────────────────
// In ensemble mode, INFO/WARNING/NORMAL go to per-task log files instead of
// stdout. ERROR always goes to stdout (it indicates a real failure).
// ─────────────────────────────────────────────────────────────────────────────

#define LOGFR(...) printf(__VA_ARGS__)

#define NORMAL_LOGF(x, ...)                                                    \
  do {                                                                         \
    if (g_ensembleMode) {                                                      \
      EnsembleLogger::Instance().LogToFile(g_ensembleTaskIndex, x "\n",        \
                                           __VA_ARGS__);                       \
    } else {                                                                   \
      printf(x, __VA_ARGS__);                                                  \
    }                                                                          \
  } while (0)

#define DEBUG_LOGF(x, ...)                                                     \
  do {                                                                         \
    if (g_ensembleMode) {                                                      \
      EnsembleLogger::Instance().LogToFile(g_ensembleTaskIndex,                \
          "DEBUG:" __FILE__ "(%i): " x "\n", __LINE__, __VA_ARGS__);           \
    } else {                                                                   \
      LOGFR(ANSI_COLOR_YELLOW "DEBUG:" __FILE__ "(%i): " ANSI_COLOR_RESET x   \
            "\n", __LINE__, __VA_ARGS__);                                      \
    }                                                                          \
  } while (0)

#define INFO_LOGF(x, ...)                                                      \
  do {                                                                         \
    if (g_ensembleMode) {                                                      \
      EnsembleLogger::Instance().LogToFile(g_ensembleTaskIndex,                \
          "INFO:" __FILE__ "(%i): " x "\n", __LINE__, __VA_ARGS__);            \
    } else {                                                                   \
      LOGFR(ANSI_COLOR_GREEN "INFO:" __FILE__ "(%i): " ANSI_COLOR_RESET x     \
            "\n", __LINE__, __VA_ARGS__);                                      \
    }                                                                          \
  } while (0)

#define WARNING_LOGF(x, ...)                                                   \
  do {                                                                         \
    if (g_ensembleMode) {                                                      \
      EnsembleLogger::Instance().LogToFile(g_ensembleTaskIndex,                \
          "WARNING:" __FILE__ "(%i): " x "\n", __LINE__, __VA_ARGS__);         \
    } else {                                                                   \
      LOGFR(ANSI_COLOR_YELLOW "WARNING:" __FILE__ "(%i): " ANSI_COLOR_RESET x \
            "\n", __LINE__, __VA_ARGS__);                                      \
    }                                                                          \
  } while (0)

// Errors ALWAYS go to stdout — they indicate fatal issues
#define ERROR_LOG(x)                                                           \
  LOGFR(ANSI_COLOR_RED "ERROR:" __FILE__ "(%i): " ANSI_COLOR_RESET x "\n",    \
        __LINE__)
#define ERROR_LOGF(x, ...)                                                     \
  LOGFR(ANSI_COLOR_RED "ERROR:" __FILE__ "(%i): " ANSI_COLOR_RESET x "\n",    \
        __LINE__, __VA_ARGS__)

#else
#include "EF5Windows.h"
#define NORMAL_LOGF(x, ...) addConsoleText(NORMAL, x, __VA_ARGS__)
#define DEBUG_LOGF(x, ...) addConsoleText(INFOFMATION, x, __VA_ARGS__)
#define INFO_LOGF(x, ...) addConsoleText(INFORMATION, x, __VA_ARGS__)
#define WARNING_LOGF(x, ...) addConsoleText(WARNING, x, __VA_ARGS__)
#define ERROR_LOG(x) addConsoleText(FATAL, "%s", x)
#define ERROR_LOGF(x, ...) addConsoleText(FATAL, x, __VA_ARGS__)
#endif

#endif
