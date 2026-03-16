#pragma once

#include <chrono>

#include "app_config.h"
#include "mouse_driver.h"

namespace aim {

struct AimDebugSnapshot {
    bool valid = false;
    bool in_deadzone = false;
    bool use_relative_mode = false;
    float source_x = 0.0f;
    float source_y = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float error_x = 0.0f;
    float error_y = 0.0f;
    float error_dist = 0.0f;
    float move_raw_x = 0.0f;
    float move_raw_y = 0.0f;
    float move_post_gain_x = 0.0f;
    float move_post_gain_y = 0.0f;
    float move_filtered_x = 0.0f;
    float move_filtered_y = 0.0f;
    int cmd_dx = 0;
    int cmd_dy = 0;
    float dt_seconds = 0.0f;
};

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
    const AimDebugSnapshot& LastDebug() const { return last_debug_; }

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
    float near_zone_lpf_alpha_ = config::kAimNearZoneLpfAlpha;
    float sign_flip_damping_ = config::kAimSignFlipDamping;
    float output_overshoot_ratio_ = config::kAimOutputOvershootRatio;
    float near_zone_max_cmd_ratio_ = config::kAimNearZoneMaxCmdRatio;
    float micro_error_px_ = config::kAimMicroErrorPx;
    float micro_move_deadband_px_ = config::kAimMicroMoveDeadbandPx;
    float flip_suppress_error_px_ = config::kAimFlipSuppressErrorPx;
    int flip_suppress_cmd_px_ = config::kAimFlipSuppressCmdPx;

    bool pid_initialized_ = false;
    float prev_error_x_ = 0.0f;
    float prev_error_y_ = 0.0f;
    float integral_x_ = 0.0f;
    float integral_y_ = 0.0f;
    float derivative_x_ = 0.0f;
    float derivative_y_ = 0.0f;
    float filtered_move_x_ = 0.0f;
    float filtered_move_y_ = 0.0f;
    int prev_cmd_dx_ = 0;
    int prev_cmd_dy_ = 0;
    std::chrono::steady_clock::time_point last_update_time_{};
    AimDebugSnapshot last_debug_{};
};

} // namespace aim
