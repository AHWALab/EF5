#pragma once

#include <cstdio>
#include <cstring>
#include <chrono>
#include <cmath>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

namespace ef5
{

    class RuntimeStatsReporter
    {
    public:
        RuntimeStatsReporter() { startWall = std::chrono::steady_clock::now(); }

        ~RuntimeStatsReporter()
        {
            const auto endWall = std::chrono::steady_clock::now();
            const double wallSeconds =
                std::chrono::duration_cast<std::chrono::duration<double>>(endWall -
                                                                          startWall)
                    .count();

            rusage usage;
            std::memset(&usage, 0, sizeof(usage));
            getrusage(RUSAGE_SELF, &usage);

            const double cpuUser = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6;
            const double cpuSys = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
            const double cpuSeconds = cpuUser + cpuSys;

            const double minReliableWall = 0.05; // seconds
            const bool utilizationReliable = wallSeconds >= minReliableWall;
            const double cpuToWall = utilizationReliable && wallSeconds > 0.0
                                         ? (cpuSeconds / wallSeconds)
                                         : 0.0;
            const double approxCoresUsed = cpuToWall;
            const int recommendedVcpus = utilizationReliable && approxCoresUsed > 0.0
                                             ? static_cast<int>(
                                                   std::ceil(approxCoresUsed))
                                             : -1;

            const double maxRssMiB = usage.ru_maxrss / 1024.0; // ru_maxrss in KiB on Linux
            const double currentRssMiB = readCurrentRssMiB();

            std::printf("\n===== Runtime resource summary =====\n");
            std::printf("Wall time: %.6f s\n", wallSeconds);
            std::printf("CPU time:  user %.3f s, sys %.3f s, total %.3f s\n", cpuUser,
                        cpuSys, cpuSeconds);
            if (utilizationReliable)
            {
                std::printf("Avg CPU utilization: %.1f%% (approx cores used: %.2f)\n",
                            cpuToWall * 100.0, approxCoresUsed);
                std::printf("Recommended vCPUs: %d\n", recommendedVcpus);
            }
            else
            {
                std::printf(
                    "Avg CPU utilization: n/a (run too short for reliable estimate)\n");
            }
            if (maxRssMiB > 0.0)
            {
                std::printf("Peak RSS: %.2f MiB\n", maxRssMiB);
            }
            if (currentRssMiB > 0.0)
            {
                std::printf("Current RSS: %.2f MiB\n", currentRssMiB);
            }
            std::printf("====================================\n\n");
        }

    private:
        std::chrono::steady_clock::time_point startWall;

        static double readCurrentRssMiB()
        {
            long long kib = readCurrentRssKiBFromProcStatus();
            if (kib >= 0)
                return static_cast<double>(kib) / 1024.0;
            long long kib2 = readCurrentRssKiBFromProcStatm();
            if (kib2 >= 0)
                return static_cast<double>(kib2) / 1024.0;
            return -1.0;
        }

        static long long readCurrentRssKiBFromProcStatus()
        {
            FILE *f = std::fopen("/proc/self/status", "r");
            if (!f)
                return -1;
            char line[256];
            long long kib = -1;
            while (std::fgets(line, sizeof(line), f))
            {
                if (std::strncmp(line, "VmRSS:", 6) == 0)
                {
                    // Expected format: "VmRSS:\t  123456 kB"
                    char *p = line + 6;
                    while (*p == ' ' || *p == '\t')
                        ++p;
                    long long val = 0;
                    while (*p >= '0' && *p <= '9')
                    {
                        val = val * 10 + (*p - '0');
                        ++p;
                    }
                    kib = val;
                    break;
                }
            }
            std::fclose(f);
            return kib;
        }

        static long long readCurrentRssKiBFromProcStatm()
        {
            FILE *f = std::fopen("/proc/self/statm", "r");
            if (!f)
                return -1;
            long long sizePages = 0;
            long long residentPages = 0;
            long pageSize = sysconf(_SC_PAGESIZE);
            if (std::fscanf(f, "%lld %lld", &sizePages, &residentPages) == 2)
            {
                std::fclose(f);
                long long bytes = residentPages * static_cast<long long>(pageSize);
                return bytes / 1024; // KiB
            }
            std::fclose(f);
            return -1;
        }
    };

} // namespace ef5
