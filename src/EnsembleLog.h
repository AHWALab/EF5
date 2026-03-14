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
#define ENS_RESET       "\x1b[0m"
#define ENS_BOLD        "\x1b[1m"
#define ENS_DIM         "\x1b[2m"
#define ENS_ITALIC      "\x1b[3m"
#define ENS_UNDERLINE   "\x1b[4m"

// Foreground colors
#define ENS_FG_RED      "\x1b[31m"
#define ENS_FG_GREEN    "\x1b[32m"
#define ENS_FG_YELLOW   "\x1b[33m"
#define ENS_FG_BLUE     "\x1b[34m"
#define ENS_FG_MAGENTA  "\x1b[35m"
#define ENS_FG_CYAN     "\x1b[36m"
#define ENS_FG_WHITE    "\x1b[37m"

// Bright foreground colors
#define ENS_FG_BRED     "\x1b[91m"
#define ENS_FG_BGREEN   "\x1b[92m"
#define ENS_FG_BYELLOW  "\x1b[93m"
#define ENS_FG_BBLUE    "\x1b[94m"
#define ENS_FG_BMAGENTA "\x1b[95m"
#define ENS_FG_BCYAN    "\x1b[96m"
#define ENS_FG_BWHITE   "\x1b[97m"

// Cursor control
#define ENS_CURSOR_UP(n)    "\x1b[" #n "A"
#define ENS_CLEAR_LINE      "\x1b[2K"
#define ENS_CURSOR_SAVE     "\x1b[s"
#define ENS_CURSOR_RESTORE  "\x1b[u"
#define ENS_HIDE_CURSOR     "\x1b[?25l"
#define ENS_SHOW_CURSOR     "\x1b[?25h"

// ─────────────────────────────────────────────────────────────────────────────
// Color palette for assigning unique colors to ensemble members
// ─────────────────────────────────────────────────────────────────────────────
static const char* ENS_TASK_COLORS[] = {
    ENS_FG_BCYAN,
    ENS_FG_BGREEN,
    ENS_FG_BYELLOW,
    ENS_FG_BMAGENTA,
    ENS_FG_BBLUE,
    ENS_FG_BRED,
    ENS_FG_CYAN,
    ENS_FG_GREEN,
    ENS_FG_YELLOW,
    ENS_FG_MAGENTA,
};
static const int ENS_NUM_COLORS = sizeof(ENS_TASK_COLORS) / sizeof(ENS_TASK_COLORS[0]);

// ─────────────────────────────────────────────────────────────────────────────
// Unicode box-drawing and progress bar characters
// ─────────────────────────────────────────────────────────────────────────────
#define ENS_BAR_FILLED  "█"
#define ENS_BAR_PARTIAL "░"
#define ENS_BOX_TL      "┌"
#define ENS_BOX_TR      "┐"
#define ENS_BOX_BL      "└"
#define ENS_BOX_BR      "┘"
#define ENS_BOX_H       "─"
#define ENS_BOX_V       "│"
#define ENS_CHECK       "✓"
#define ENS_CROSS       "✗"
#define ENS_ARROW       "►"
#define ENS_CLOCK       "⏱"
#define ENS_SPINNER_CHARS "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"

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
    char currentTimeStr[64];
    std::mutex timeMutex;

    EnsembleTaskInfo()
        : color(ENS_FG_WHITE), completedSteps(0), totalSteps(0),
          finished(false), failed(false), startTime(0), endTime(0) {
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
        spinnerIdx_ = 0;
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

    EnsembleTaskInfo* GetTask(int taskIdx) {
        if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
            return tasks_[taskIdx];
        }
        return nullptr;
    }

    // Thread-safe log with task prefix
    void Log(int taskIdx, const char* fmt, ...) {
        char msgBuf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
        va_end(args);

        std::lock_guard<std::mutex> lock(logMutex_);
        if (taskIdx >= 0 && taskIdx < (int)tasks_.size()) {
            printf("%s%s[%-20s]%s %s\n",
                   ENS_BOLD, tasks_[taskIdx]->color,
                   tasks_[taskIdx]->name.c_str(),
                   ENS_RESET, msgBuf);
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

        // In TTY mode, we don't spam per-timestep logs — the progress bar handles it
        // In non-TTY mode (file redirect), log every Nth step
        if (!isTTY_) {
            int step = tasks_[taskIdx]->completedSteps.load();
            int total = tasks_[taskIdx]->totalSteps;
            // Log every 10% milestone
            if (total > 0 && step % std::max(1, total / 10) == 0) {
                std::lock_guard<std::mutex> lock(logMutex_);
                printf("[%-20s] %3.0f%% complete  (%s)",
                       tasks_[taskIdx]->name.c_str(),
                       tasks_[taskIdx]->GetProgress(), timeStr);
                if (stepSec >= 0) printf("  %.3f sec/step", stepSec);
                printf("\n");
                fflush(stdout);
            }
        }
    }

    // Print the ensemble header banner
    void PrintBanner(int numTasks) {
        std::lock_guard<std::mutex> lock(logMutex_);
        printf("\n");
        PrintHRule('=', 74);
        printf("  %s%s%s ENSEMBLE SIMULATION %s%s\n",
               ENS_BOLD, ENS_FG_BCYAN, ENS_ARROW, ENS_CLOCK, ENS_RESET);
        printf("  %s%d task(s) queued for parallel execution%s\n",
               ENS_DIM, numTasks, ENS_RESET);
        PrintHRule('=', 74);
        printf("\n");
        fflush(stdout);
    }

    // Print task summary table before execution
    void PrintTaskTable() {
        std::lock_guard<std::mutex> lock(logMutex_);
        printf("  %s%s%-4s  %-22s  %-8s  %-8s  %-10s%s\n",
               ENS_BOLD, ENS_UNDERLINE,
               "#", "Task Name", "Model", "Precip", "Steps",
               ENS_RESET);
        for (int i = 0; i < (int)tasks_.size(); i++) {
            printf("  %s%s%-4d%s  %s%-22s%s  %-8s  %-8s  %-10d\n",
                   ENS_BOLD, tasks_[i]->color, i + 1, ENS_RESET,
                   tasks_[i]->color, tasks_[i]->name.c_str(), ENS_RESET,
                   "—", "—", tasks_[i]->totalSteps);
        }
        printf("\n");
        fflush(stdout);
    }

    // Draw the progress bar (call periodically from main/progress thread)
    void DrawProgress() {
        if (!isTTY_ || !active_) return;

        std::lock_guard<std::mutex> lock(logMutex_);

        int numTasks = (int)tasks_.size();
        int barWidth = 30;
        double elapsed = GetWallTime() - startWallTime_;

        // Move cursor up to overwrite previous progress (numTasks + 3 header/footer lines)
        if (progressDrawn_) {
            printf("\x1b[%dA", numTasks + 4);
        }

        // Top border
        printf("%s%s" ENS_BOX_TL, ENS_FG_CYAN, ENS_DIM);
        for (int i = 0; i < 72; i++) printf(ENS_BOX_H);
        printf(ENS_BOX_TR "%s\n", ENS_RESET);

        // Per-task progress bars
        for (int i = 0; i < numTasks; i++) {
            EnsembleTaskInfo* t = tasks_[i];
            float pct = t->GetProgress();
            int filled = (int)(pct / 100.0f * barWidth);
            if (filled > barWidth) filled = barWidth;

            std::string timeStr = t->GetCurrentTime();

            printf("%s" ENS_BOX_V "%s ", ENS_FG_CYAN, ENS_RESET);

            // Status icon
            if (t->finished.load()) {
                if (t->failed.load()) {
                    printf("%s%s " ENS_CROSS "%s", ENS_BOLD, ENS_FG_RED, ENS_RESET);
                } else {
                    printf("%s%s " ENS_CHECK "%s", ENS_BOLD, ENS_FG_BGREEN, ENS_RESET);
                }
            } else {
                printf("%s%s %c%s", ENS_BOLD, t->color,
                       GetSpinnerChar(), ENS_RESET);
            }

            // Task name (color-coded)
            printf(" %s%-18s%s ", t->color, t->name.c_str(), ENS_RESET);

            // Progress bar
            printf("%s", t->color);
            for (int j = 0; j < filled; j++) printf(ENS_BAR_FILLED);
            printf("%s", ENS_DIM);
            for (int j = filled; j < barWidth; j++) printf(ENS_BAR_PARTIAL);
            printf("%s", ENS_RESET);

            // Percentage and current time
            if (t->finished.load()) {
                double taskTime = t->endTime - t->startTime;
                printf(" %s%3.0f%%%s %s(%.1fs)%s",
                       ENS_FG_BGREEN, pct, ENS_RESET,
                       ENS_DIM, taskTime, ENS_RESET);
            } else {
                printf(" %s%3.0f%%%s %s%s%s",
                       ENS_FG_WHITE, pct, ENS_RESET,
                       ENS_DIM, timeStr.c_str(), ENS_RESET);
            }

            // Pad to box width and close
            printf("  %s" ENS_BOX_V "%s\n", ENS_FG_CYAN, ENS_RESET);
        }

        // Overall progress line
        float overallPct = GetOverallProgress();
        int overallFilled = (int)(overallPct / 100.0f * 40);
        if (overallFilled > 40) overallFilled = 40;

        printf("%s" ENS_BOX_V "%s ", ENS_FG_CYAN, ENS_RESET);
        printf(" %s%sOverall:%s ", ENS_BOLD, ENS_FG_WHITE, ENS_RESET);
        printf("%s" ENS_FG_BWHITE, ENS_BOLD);
        for (int j = 0; j < overallFilled; j++) printf(ENS_BAR_FILLED);
        printf("%s", ENS_DIM);
        for (int j = overallFilled; j < 40; j++) printf(ENS_BAR_PARTIAL);
        printf("%s", ENS_RESET);
        printf(" %s%3.0f%%%s  %s%s%s%s       %s" ENS_BOX_V "%s\n",
               ENS_FG_BWHITE, overallPct, ENS_RESET,
               ENS_DIM, ENS_CLOCK, " ", FormatTime(elapsed).c_str(), 
               ENS_FG_CYAN, ENS_RESET);

        // Bottom border
        printf("%s%s" ENS_BOX_BL, ENS_FG_CYAN, ENS_DIM);
        for (int i = 0; i < 72; i++) printf(ENS_BOX_H);
        printf(ENS_BOX_BR "%s\n", ENS_RESET);

        fflush(stdout);
        progressDrawn_ = true;
        spinnerIdx_++;
    }

    // Print final summary after all tasks complete
    void PrintSummary() {
        std::lock_guard<std::mutex> lock(logMutex_);
        double totalTime = GetWallTime() - startWallTime_;

        printf("\n");
        PrintHRule('=', 74);
        printf("  %s%s" ENS_CHECK " ENSEMBLE SIMULATION COMPLETE%s\n",
               ENS_BOLD, ENS_FG_BGREEN, ENS_RESET);
        PrintHRule('-', 74);

        printf("  %s%-22s  %6s  %10s  %s%s\n",
               ENS_UNDERLINE, "Task", "Steps", "Time", "Status", ENS_RESET);

        for (int i = 0; i < (int)tasks_.size(); i++) {
            EnsembleTaskInfo* t = tasks_[i];
            double taskTime = t->endTime - t->startTime;
            const char* status = t->failed.load()
                ? (ENS_FG_RED ENS_BOLD ENS_CROSS " FAILED" ENS_RESET)
                : (ENS_FG_BGREEN ENS_BOLD ENS_CHECK " OK" ENS_RESET);

            printf("  %s%-22s%s  %6d  %8.1fs  %s\n",
                   t->color, t->name.c_str(), ENS_RESET,
                   t->completedSteps.load(), taskTime, status);
        }

        PrintHRule('-', 74);
        printf("  %sTotal wall time:%s %s%s%.1f seconds%s\n",
               ENS_DIM, ENS_RESET, ENS_BOLD, ENS_FG_BCYAN, totalTime, ENS_RESET);
        PrintHRule('=', 74);
        printf("\n");
        fflush(stdout);
    }

    void Shutdown() {
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
    EnsembleLogger() : active_(false), isTTY_(false), progressDrawn_(false),
                       startWallTime_(0), spinnerIdx_(0) {}
    ~EnsembleLogger() { Shutdown(); }
    EnsembleLogger(const EnsembleLogger&) = delete;
    EnsembleLogger& operator=(const EnsembleLogger&) = delete;

    void PrintHRule(char ch, int width) {
        for (int i = 0; i < width; i++) putchar(ch);
        putchar('\n');
    }

    char GetSpinnerChar() {
        const char* spinner = "/-\\|";
        return spinner[spinnerIdx_ % 4];
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
    bool progressDrawn_;
    double startWallTime_;
    int spinnerIdx_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global ensemble mode flag (checked by Simulator to decide logging strategy)
// ─────────────────────────────────────────────────────────────────────────────
extern bool g_ensembleMode;
extern thread_local int g_ensembleTaskIndex;

#endif // ENSEMBLE_LOG_H
