#pragma once

#include "app_config.h"

namespace aim {

struct RuntimeTuning {
    bool aim_enabled = false;
    float conf_threshold = config::kConfThreshold;
    float nms_iou_threshold = config::kNmsIouThreshold;
    float active_circle_radius_px = config::kAimActiveCircleRadiusPx;
    float aim_smooth_factor = config::kAimSmoothFactor;
    float aim_max_step_px = config::kAimMaxStepPx;
    float aim_deadzone_px = config::kAimDeadzonePx;
    bool preview_enabled = config::kShowPreviewWindow;
    bool verbose_log_enabled = config::kEnableVerboseLog;
    bool request_exit = false;
};

} // namespace aim
