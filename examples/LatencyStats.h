/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-05 14:24:03
 * @FilePath: /EdgeVision/examples/LatencyStats.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include <vector>
#include <cstdint>

class LatencyStats {
public:
    void add(double intervalMs);
    void reset();

    double mean() const;
    double min() const;
    double max() const;
    double stddev() const;

    size_t count() const;

private:
    std::vector<double> samples;
};

#include "LatencyStats.h"

#include <numeric>
#include <algorithm>
#include <cmath>

void LatencyStats::add(double intervalMs) {
    samples.push_back(intervalMs);
}

void LatencyStats::reset() {
    samples.clear();
}

size_t LatencyStats::count() const {
    return samples.size();
}

double LatencyStats::mean() const {
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    return sum / samples.size();
}

double LatencyStats::min() const {
    return *std::min_element(samples.begin(), samples.end());
}

double LatencyStats::max() const {
    return *std::max_element(samples.begin(), samples.end());
}

double LatencyStats::stddev() const {
    double m = mean();
    double var = 0.0;

    for (double v : samples) {
        var += (v - m) * (v - m);
    }

    var /= samples.size();
    return std::sqrt(var);
}
