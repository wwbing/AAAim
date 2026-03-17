#pragma once

#include <chrono>

namespace aim {

struct PerfSample {
    double capture_ms = 0.0;
    double infer_ms = 0.0;
    double decode_ms = 0.0;
    bool capture_ok = false;
    bool infer_ok = false;
};

class PerfLogger {
public:
    void AddSample(const PerfSample& sample);
    bool ShouldPrint(int interval_ms) const;
    void PrintAndReset();
    void ResetWindow();

private:
    std::chrono::steady_clock::time_point window_start_ = std::chrono::steady_clock::now();
    int frames_ = 0;
    int infer_attempts_ = 0;
    int decode_count_ = 0;
    double capture_sum_ms_ = 0.0;
    double infer_sum_ms_ = 0.0;
    double decode_sum_ms_ = 0.0;
};

} // namespace aim
