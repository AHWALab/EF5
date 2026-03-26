#ifndef ENSEMBLE_LOG_H
#define ENSEMBLE_LOG_H

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cmath>
#include <ctime>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────────────────────
// ANSI color codes for rich terminal output
// ─────────────────────────────────────────────────────────────────────────────
#define ENS_RESET "\x1b[0m"
#define ENS_BOLD "\x1b[1m"
#define ENS_DIM "\x1b[2m"
#define ENS_ITALIC "\x1b[3m"
#define ENS_UNDERLINE "\x1b[4m"

// Foreground colors
#define ENS_FG_RED "\x1b[31m"
#define ENS_FG_GREEN "\x1b[32m"
#define ENS_FG_YELLOW "\x1b[33m"
#define ENS_FG_BLUE "\x1b[34m"
#define ENS_FG_MAGENTA "\x1b[35m"
#define ENS_FG_CYAN "\x1b[36m"
#define ENS_FG_WHITE "\x1b[37m"

// Bright foreground colors
#define ENS_FG_BRED "\x1b[91m"
#define ENS_FG_BGREEN "\x1b[92m"
#define ENS_FG_BYELLOW "\x1b[93m"
#define ENS_FG_BBLUE "\x1b[94m"
#define ENS_FG_BMAGENTA "\x1b[95m"
#define ENS_FG_BCYAN "\x1b[96m"
#define ENS_FG_BWHITE "\x1b[97m"

// Progress bar characters
#define ENS_BAR_FILLED "\xe2\x96\x88"
#define ENS_BAR_PARTIAL "\xe2\x96\x91"
#define ENS_CHECK "\xe2\x9c\x93"
#define ENS_CROSS "\xe2\x9c\x97"
#define ENS_ARROW ">>"

// ─────────────────────────────────────────────────────────────────────────────
// Color palette for assigning unique colors to ensemble members
// ─────────────────────────────────────────────────────────────────────────────
static const char* ENS_TASK_COLORS[] = {
    ENS_FG_BCYAN, ENS_FG_BGREEN, ENS_FG_BYELLOW, ENS_FG_BMAGENTA, ENS_FG_BBLUE,
    ENS_FG_BRED,  ENS_FG_CYAN,   ENS_FG_GREEN,   ENS_FG_YELLOW,   ENS_FG_MAGENTA,
};
static const int ENS_NUM_COLORS = sizeof(ENS_TASK_COLORS) / sizeof(ENS_TASK_COLORS[0]);

// ─────────────────────────────────────────────────────────────────────────────
// EnsembleTaskInfo — Tracks progress for one ensemble member
// ─────────────────────────────────────────────────────────────────────────────
struct EnsembleTaskInfo {
  std::string name;
  const char* color;
  std::atomic<int> completedSteps;
  int totalSteps;
  std::atomic<bool> finished;
  std::atomic<bool> failed;
  double startTime;
  double endTime;
  int missingFiles;
  FILE* logFile;
  std::string logFilePath;
  char currentTimeStr[64];
  std::mutex timeMutex;

  EnsembleTaskInfo()
      : color(ENS_FG_WHITE),
        completedSteps(0),
        totalSteps(0),
        finished(false),
        failed(false),
        startTime(0),
        endTime(0),
        missingFiles(0),
        logFile(nullptr) {
    currentTimeStr[0] = '\0';
  }

  // Cannot copy atomics, so delete copy and provide move
  EnsembleTaskInfo(const EnsembleTaskInfo&) = delete;
  EnsembleTaskInfo& operator=(const EnsembleTaskInfo&) = delete;

  void SetCurrentTime(const char* timeStr) {
    std::lock_guard<std::mutex> lock(timeMutex);
    strncpy(currentTimeStr, timeStr, sizeof(currentTimeStr) - 1);
    currentTimeStr[sizeof(currentTimeStr) - 1] = '\0';
  }

  std::string GetCurrentTime() {
    std::lock_guard<std::mutex> lock(timeMutex);
    return std::string(currentTimeStr);
  }

  float GetProgress() const {
    if (totalSteps <= 0) return 0.0f;
    return (float)completedSteps.load() / (float)totalSteps * 100.0f;
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// EnsembleLogger — Thread-safe logging + progress bar for ensemble runs
// ─────────────────────────────────────────────────────────────────────────────
class EnsembleLogger {
 public:
  static EnsembleLogger& Instance() {
    static EnsembleLogger instance;
    return instance;
  }

  void Initialize(int numTasks) {
    tasks_.resize(numTasks);
    for (int i = 0; i < numTasks; i++) {
      tasks_[i] = new EnsembleTaskInfo();
      tasks_[i]->color = ENS_TASK_COLORS[i % ENS_NUM_COLORS];
    }
    active_ = true;
    isTTY_ = isatty(STDOUT_FILENO) != 0;
    startWallTime_ = GetWallTime();
    lastMilestone_ = -1;
  }

  void SetTaskName(int taskIdx, const char* name) {
    if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
      tasks_[taskIdx]->name = name;
    }
  }

  void SetTaskTotalSteps(int taskIdx, int totalSteps) {
    if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
      tasks_[taskIdx]->totalSteps = totalSteps;
    }
  }

  void SetTaskStartTime(int taskIdx) {
    if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
      tasks_[taskIdx]->startTime = GetWallTime();
    }
  }

  void SetTaskFinished(int taskIdx, bool success = true) {
    if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
      tasks_[taskIdx]->finished.store(true);
      tasks_[taskIdx]->failed.store(!success);
      tasks_[taskIdx]->endTime = GetWallTime();
    }
  }

  void SetTaskMissingFiles(int taskIdx, int count) {
    if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
      tasks_[taskIdx]->missingFiles = count;
    }
  }

  EnsembleTaskInfo* GetTask(int taskIdx) {
    if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
      return tasks_[taskIdx];
    }
    return nullptr;
  }

  bool IsTaskFinished(int taskIdx) const {
    if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
      return tasks_[taskIdx]->finished.load();
    }
    return true;  // Non-existent tasks are considered "finished"
  }

  // ── Per-task log file management ────────────────────────────────────────
  void OpenTaskLogFile(int taskIdx, const char* outputDir) {
    if (taskIdx < 0 || taskIdx >= (int)tasks_.size()) return;

    char logPath[1024];
    snprintf(logPath, sizeof(logPath), "%s/ensemble_task_%s.log", outputDir,
             tasks_[taskIdx]->name.c_str());
    tasks_[taskIdx]->logFilePath = logPath;

    FILE* f = fopen(logPath, "w");
    if (f) {
      tasks_[taskIdx]->logFile = f;
      // Write log header
      time_t now = time(NULL);
      char timeBuf[64];
      strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
      fprintf(f, "======================================================\n");
      fprintf(f, " EF5 Ensemble Task Log: %s\n", tasks_[taskIdx]->name.c_str());
      fprintf(f, " Started: %s\n", timeBuf);
      fprintf(f, "======================================================\n\n");
      fflush(f);
    }
  }

  void CloseTaskLogFiles() {
    for (auto* t : tasks_) {
      if (t->logFile) {
        fprintf(t->logFile, "\n--------------------------------------\n");
        fprintf(t->logFile, " Task finished. Steps: %d, Missing: %d\n", t->completedSteps.load(),
                t->missingFiles);
        fprintf(t->logFile, "--------------------------------------\n");
        fclose(t->logFile);
        t->logFile = nullptr;
      }
    }
  }

  // Thread-safe log to per-task file (used by macros in ensemble mode)
  void LogToFile(int taskIdx, const char* fmt, ...) {
    if (taskIdx < 0 || taskIdx >= (int)tasks_.size()) return;
    FILE* f = tasks_[taskIdx]->logFile;
    if (!f) return;

    char msgBuf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(logMutex_);
    fprintf(f, "%s", msgBuf);
    fflush(f);
  }

  // Thread-safe log with task prefix (console output)
  void Log(int taskIdx, const char* fmt, ...) {
    char msgBuf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(logMutex_);
    if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
      printf("[%-20s] %s\n", tasks_[taskIdx]->name.c_str(), msgBuf);
    } else {
      printf("%s\n", msgBuf);
    }
    fflush(stdout);
  }

  // Thread-safe timestep log (more compact, used per simulation step)
  void LogTimestep(int taskIdx, const char* timeStr, double stepSec = -1.0) {
    if (taskIdx < 0 || taskIdx >= (int)tasks_.size()) return;

    tasks_[taskIdx]->SetCurrentTime(timeStr);
    tasks_[taskIdx]->completedSteps.fetch_add(1);
  }

  // Print the ensemble header banner
  void PrintBanner(int numTasks) {
    std::lock_guard<std::mutex> lock(logMutex_);
    printf("\n");
    PrintHRule('=', 74);
    printf("  EF5 Ensemble: %d task(s) queued for parallel execution\n", numTasks);
    PrintHRule('=', 74);
    printf("\n");
    fflush(stdout);
  }

  // ── Progress Bar ────────────────────────────────────────────────────────
  // Single overall progress bar — clean and simple for HPC use.
  void DrawProgress() {
    if (!active_) return;

    std::lock_guard<std::mutex> lock(logMutex_);

    int numTasks = (int)tasks_.size();
    int completedTasks = 0;
    for (const auto* t : tasks_) {
      if (t->finished.load()) completedTasks++;
    }
    float overallPct = GetOverallProgress();
    double elapsed = GetWallTime() - startWallTime_;
    int barWidth = 40;
    int filled = (int)(overallPct / 100.0f * barWidth);
    if (filled > barWidth) filled = barWidth;

    if (isTTY_) {
      // Overwrite the same line in TTY mode
      printf("\r  Ensemble [");
      for (int j = 0; j < filled; j++) printf(ENS_BAR_FILLED);
      for (int j = filled; j < barWidth; j++) printf(ENS_BAR_PARTIAL);
      printf("] %3.0f%% (%d/%d tasks)  %s", overallPct, completedTasks, numTasks,
             FormatTime(elapsed).c_str());
      fflush(stdout);
    } else {
      // In non-TTY mode, only print at 10% milestones
      int milestone = (int)(overallPct / 10.0f);
      if (milestone > lastMilestone_) {
        lastMilestone_ = milestone;
        printf("  Ensemble: %3.0f%% (%d/%d tasks)  %s\n", overallPct, completedTasks, numTasks,
               FormatTime(elapsed).c_str());
        fflush(stdout);
      }
    }
  }

  // ── Final Summary ───────────────────────────────────────────────────────
  void PrintSummary() {
    std::lock_guard<std::mutex> lock(logMutex_);
    double totalTime = GetWallTime() - startWallTime_;
    int numTasks = (int)tasks_.size();
    int okCount = 0, failCount = 0;
    for (const auto* t : tasks_) {
      if (t->failed.load())
        failCount++;
      else
        okCount++;
    }

    // Move to next line after the progress bar
    if (isTTY_) printf("\n");
    printf("\n");
    PrintHRule('=', 74);
    if (failCount == 0) {
      printf("  ENSEMBLE COMPLETE -- %d/%d tasks OK | Wall time: %.1fs\n", okCount, numTasks,
             totalTime);
    } else {
      printf("  ENSEMBLE COMPLETE -- %d OK, %d FAILED | Wall time: %.1fs\n", okCount, failCount,
             totalTime);
    }
    PrintHRule('-', 74);

    // Only show per-task details if there were failures
    if (failCount > 0) {
      printf("  %-24s %7s %9s %9s   %s\n", "Task", "Steps", "Time", "Missing", "Status");
      PrintHRule('-', 74);
      for (int i = 0; i < numTasks; i++) {
        EnsembleTaskInfo* t = tasks_[i];
        double taskTime = t->endTime - t->startTime;
        const char* status = t->failed.load() ? "FAILED" : "OK";
        printf("  %-24s %7d %7.1fs   %7d   %s\n", t->name.c_str(), t->completedSteps.load(),
               taskTime, t->missingFiles, status);
        if (!t->logFilePath.empty()) {
          printf("    Log: %s\n", t->logFilePath.c_str());
        }
      }
      PrintHRule('-', 74);
    }

    printf("  All task logs saved to output directories.\n");
    PrintHRule('=', 74);
    printf("\n");
    fflush(stdout);
  }

  void Shutdown() {
    CloseTaskLogFiles();
    active_ = false;
    for (auto* t : tasks_) {
      delete t;
    }
    tasks_.clear();
  }

  bool IsActive() const { return active_; }
  bool IsTTY() const { return isTTY_; }

  float GetOverallProgress() const {
    int totalSteps = 0, completedSteps = 0;
    for (const auto* t : tasks_) {
      totalSteps += t->totalSteps;
      completedSteps += t->completedSteps.load();
    }
    if (totalSteps <= 0) return 0.0f;
    return (float)completedSteps / (float)totalSteps * 100.0f;
  }

  bool AllFinished() const {
    for (const auto* t : tasks_) {
      if (!t->finished.load()) return false;
    }
    return true;
  }

 private:
  EnsembleLogger()
      : active_(false), isTTY_(false), startWallTime_(0), lastMilestone_(-1) {}
  ~EnsembleLogger() { Shutdown(); }
  EnsembleLogger(const EnsembleLogger&) = delete;
  EnsembleLogger& operator=(const EnsembleLogger&) = delete;

  void PrintHRule(char ch, int width) {
    for (int i = 0; i < width; i++) putchar(ch);
    putchar('\n');
  }

  static std::string FormatTime(double seconds) {
    int h = (int)(seconds / 3600);
    int m = (int)(fmod(seconds, 3600) / 60);
    int s = (int)(fmod(seconds, 60));
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    return std::string(buf);
  }

  static double GetWallTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
  }

  std::vector<EnsembleTaskInfo*> tasks_;
  std::mutex logMutex_;
  bool active_;
  bool isTTY_;
  double startWallTime_;
  int lastMilestone_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global ensemble mode flag (checked by Simulator to decide logging strategy)
// ─────────────────────────────────────────────────────────────────────────────
extern bool g_ensembleMode;
extern thread_local int g_ensembleTaskIndex;

#endif  // ENSEMBLE_LOG_H
