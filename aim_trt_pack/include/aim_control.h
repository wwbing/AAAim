#pragma once

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

    void MoveToTarget(
        MouseController& mouse,
        float target_x,
        float target_y,
        int screen_width,
        int screen_height) const;

private:
    static unsigned short ToDllCoord(float pixel_value, int pixel_max);

private:
    float smooth_factor_ = config::kAimSmoothFactor;
    float max_step_px_ = config::kAimMaxStepPx;
    float deadzone_px_ = config::kAimDeadzonePx;
    float center_lock_threshold_px_ = config::kCursorLockCenterThresholdPx;
};

} // namespace aim
