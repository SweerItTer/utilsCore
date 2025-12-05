#include "base.h"
#include <sstream>
#include <set>

class MemoryMonitor : public ResourceMonitor {
private:
    bool sampleUsage(float& usage) override {
        std::ifstream file("/proc/meminfo");
        if (!file.is_open()) {
            std::cerr << "Failed to open /proc/meminfo\n";
            return false;
        }

        unsigned long long total = 0, free = 0;
        std::set<std::string> needed = {"MemTotal:", "MemFree:"};
        std::string line;

        while (std::getline(file, line) && !needed.empty()) {
            std::istringstream iss(line);
            std::string key;
            unsigned long long value;
            iss >> key >> value;
            if (key == "MemTotal:") {
                total = value;
                needed.erase(key);
            } else if (key == "MemFree:") {
                free = value;
                needed.erase(key);
            }
        }
        file.close();

        if (total == 0) {
            std::cerr << "Invalid meminfo format\n";
            return false;
        }

        usage = 100.0f * (total - free) / total; // (used + buff/cache) / total
        return true;
    }

public:
    MemoryMonitor(int sleeptime = 1000)
        : ResourceMonitor(sleeptime, "/tmp/memory_usage") {}
};