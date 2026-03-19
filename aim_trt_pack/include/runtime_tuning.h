#pragma once

#include "app_config.h"
#include <string>

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

    // 启动阶段状态（用于 ImGui 显示初始化进度）
    bool init_in_progress = false;
    bool init_failed = false;
    float init_progress = 0.0f;          // 0~1；<0 表示不定进度（活动指示）
    std::string init_status_text;
};

} // namespace aim
