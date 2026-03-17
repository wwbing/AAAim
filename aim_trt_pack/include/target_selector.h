#pragma once

#include <vector>

#include "app_config.h"
#include "pipeline_types.h"

namespace aim {

struct TargetSelectorSettings {
    float active_radius_px = config::kAimActiveCircleRadiusPx;
    float acquire_gate_px = config::kTargetAcquireGatePx;
    float track_gate_px = config::kTargetTrackGatePx;
    int acquire_confirm_frames = config::kTargetAcquireConfirmFrames;
    int lost_tolerance_frames = config::kTargetLostToleranceFrames;
    float track_smoothing = config::kTargetTrackSmoothing;
};

class StableTargetSelector {
public:
    StableTargetSelector() = default;

    void Reset();
    void SetSettings(const TargetSelectorSettings& settings);
    const TargetSelectorSettings& Settings() const { return settings_; }

    bool Select(
        const Detections& detections,
        float capture_offset_x,
        float capture_offset_y,
        float screen_center_x,
        float screen_center_y,
        int target_class_id,
        TargetPoint& target);

private:
    struct CandidateTarget {
        float global_x = 0.0f;
        float global_y = 0.0f;
        float dist2_center = 0.0f;
    };

    static void FillTarget(
        float screen_center_x,
        float screen_center_y,
        float target_x,
        float target_y,
        TargetPoint& out);

    static bool FindNearestToCenter(const std::vector<CandidateTarget>& candidates, CandidateTarget& out);

    static bool FindNearestToPoint(
        const std::vector<CandidateTarget>& candidates,
        float ref_x,
        float ref_y,
        float gate_px,
        CandidateTarget& out);

    void HandleMiss();

private:
    TargetSelectorSettings settings_{};
    bool has_lock_ = false;
    float lock_x_ = 0.0f;
    float lock_y_ = 0.0f;
    int lock_lost_frames_ = 0;

    bool has_pending_ = false;
    float pending_x_ = 0.0f;
    float pending_y_ = 0.0f;
    int pending_hits_ = 0;
};

} // namespace aim

