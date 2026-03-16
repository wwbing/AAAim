#pragma once

#include <chrono>

#include "app_config.h"
#include "mouse_driver.h"

namespace aim {

class AimControl {
public:
    AimControl(
        float smooth_factor,
        float max_step_px,
        float deadzone_px,
        float center_lock_threshold_px);

    void Reset();
    void MoveToTarget(
        MouseController& mouse,
        float target_x,
        float target_y,
        int screen_width,
        int screen_height);

private:
    static float EaseOutCubic(float x);
    static unsigned short ToDllCoord(float pixel_value, int pixel_max);

private:
    config::AimAlgorithm algorithm_ = config::kAimAlgorithm;
    config::AimMoveMode move_mode_ = config::kAimMoveMode;
    float smooth_factor_ = config::kAimSmoothFactor;
    float max_step_px_ = config::kAimMaxStepPx;
    float deadzone_px_ = config::kAimDeadzonePx;
    float center_lock_threshold_px_ = config::kCursorLockCenterThresholdPx;
    float pid_ki_ = config::kAimPidKi;
    float pid_kd_ = config::kAimPidKd;
    float pid_derivative_alpha_ = config::kAimPidDerivativeAlpha;
    float pid_integral_limit_ = config::kAimPidIntegralLimit;
    float bezier_strength_ = config::kAimBezierStrength;
    float bezier_distance_px_ = config::kAimBezierDistancePx;
    float near_zone_px_ = config::kAimNearZonePx;
    float near_zone_min_gain_ = config::kAimNearZoneMinGain;
    float output_lpf_alpha_ = config::kAimOutputLpfAlpha;
    float relative_min_step_error_px_ = config::kAimRelativeMinStepErrorPx;

    bool pid_initialized_ = false;
    float prev_error_x_ = 0.0f;
    float prev_error_y_ = 0.0f;
    float integral_x_ = 0.0f;
    float integral_y_ = 0.0f;
    float derivative_x_ = 0.0f;
    float derivative_y_ = 0.0f;
    float filtered_move_x_ = 0.0f;
    float filtered_move_y_ = 0.0f;
    std::chrono::steady_clock::time_point last_update_time_{};
};

} // namespace aim
