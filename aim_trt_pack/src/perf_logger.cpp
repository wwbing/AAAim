#include "perf_logger.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace aim {

void PerfLogger::AddSample(const PerfSample& sample)
{
    ++frames_;
    capture_sum_ms_ += sample.capture_ms;

    if (sample.capture_ok)
    {
        ++infer_attempts_;
        infer_sum_ms_ += sample.infer_ms;
    }

    if (sample.infer_ok)
    {
        ++decode_count_;
        decode_sum_ms_ += sample.decode_ms;
    }
}

bool PerfLogger::ShouldPrint(int interval_ms) const
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - window_start_).count();
    return elapsed_ms >= std::max(100, interval_ms);
}

void PerfLogger::PrintAndReset()
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - window_start_).count();

    const double fps =
        (frames_ > 0 && elapsed_ms > 0) ? (1000.0 * static_cast<double>(frames_) / static_cast<double>(elapsed_ms)) : 0.0;
    const double cap_avg_ms = frames_ > 0 ? (capture_sum_ms_ / static_cast<double>(frames_)) : 0.0;
    const double infer_avg_ms = infer_attempts_ > 0 ? (infer_sum_ms_ / static_cast<double>(infer_attempts_)) : 0.0;
    const double decode_avg_ms = decode_count_ > 0 ? (decode_sum_ms_ / static_cast<double>(decode_count_)) : 0.0;
    const double total_avg_ms = cap_avg_ms + infer_avg_ms + decode_avg_ms;

    std::cout << std::fixed << std::setprecision(1)
              << "[日志] FPS=" << fps
              << " 截图(ms)=" << cap_avg_ms
              << " 推理(ms)=" << infer_avg_ms
              << " 总延迟(ms)=" << total_avg_ms
              << "\n";

    window_start_ = now;
    frames_ = 0;
    infer_attempts_ = 0;
    decode_count_ = 0;
    capture_sum_ms_ = 0.0;
    infer_sum_ms_ = 0.0;
    decode_sum_ms_ = 0.0;
}

void PerfLogger::ResetWindow()
{
    window_start_ = std::chrono::steady_clock::now();
    frames_ = 0;
    infer_attempts_ = 0;
    decode_count_ = 0;
    capture_sum_ms_ = 0.0;
    infer_sum_ms_ = 0.0;
    decode_sum_ms_ = 0.0;
}

} // namespace aim
