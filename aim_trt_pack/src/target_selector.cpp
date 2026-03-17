#include "target_selector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aim {

void StableTargetSelector::Reset()
{
    has_lock_ = false;
    lock_x_ = 0.0f;
    lock_y_ = 0.0f;
    lock_lost_frames_ = 0;
    has_pending_ = false;
    pending_x_ = 0.0f;
    pending_y_ = 0.0f;
    pending_hits_ = 0;
}

void StableTargetSelector::SetSettings(const TargetSelectorSettings& settings)
{
    settings_ = settings;
    settings_.active_radius_px = std::max(1.0f, settings_.active_radius_px);
    settings_.acquire_gate_px = std::max(1.0f, settings_.acquire_gate_px);
    settings_.track_gate_px = std::max(1.0f, settings_.track_gate_px);
    settings_.acquire_confirm_frames = std::max(1, settings_.acquire_confirm_frames);
    settings_.lost_tolerance_frames = std::max(0, settings_.lost_tolerance_frames);
    settings_.track_smoothing = std::clamp(settings_.track_smoothing, 0.0f, 0.98f);
}

bool StableTargetSelector::Select(
    const Detections& detections,
    float capture_offset_x,
    float capture_offset_y,
    float screen_center_x,
    float screen_center_y,
    int target_class_id,
    TargetPoint& target)
{
    target = {};

    const float active_radius2 = settings_.active_radius_px * settings_.active_radius_px;
    std::vector<CandidateTarget> candidates;
    candidates.reserve(detections.size());

    for (const auto& det : detections)
    {
        if (target_class_id >= 0 && static_cast<int>(det[5]) != target_class_id)
        {
            continue;
        }

        const float local_cx = (det[0] + det[2]) * 0.5f;
        const float local_cy = (det[1] + det[3]) * 0.5f;
        const float global_x = local_cx + capture_offset_x;
        const float global_y = local_cy + capture_offset_y;
        const float dx_center = global_x - screen_center_x;
        const float dy_center = global_y - screen_center_y;
        const float dist2_center = dx_center * dx_center + dy_center * dy_center;

        if (dist2_center > active_radius2)
        {
            continue;
        }

        candidates.push_back({ global_x, global_y, dist2_center });
    }

    if (candidates.empty())
    {
        HandleMiss();
        return false;
    }

    CandidateTarget chosen = {};
    if (has_lock_)
    {
        if (FindNearestToPoint(candidates, lock_x_, lock_y_, settings_.track_gate_px, chosen))
        {
            const float keep = settings_.track_smoothing;
            const float update = 1.0f - keep;
            lock_x_ = lock_x_ * keep + chosen.global_x * update;
            lock_y_ = lock_y_ * keep + chosen.global_y * update;
            lock_lost_frames_ = 0;
            FillTarget(screen_center_x, screen_center_y, lock_x_, lock_y_, target);
            return true;
        }

        HandleMiss();
        return false;
    }

    if (!FindNearestToCenter(candidates, chosen))
    {
        return false;
    }

    if (has_pending_)
    {
        const float dx = chosen.global_x - pending_x_;
        const float dy = chosen.global_y - pending_y_;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= settings_.acquire_gate_px)
        {
            pending_x_ = pending_x_ * 0.6f + chosen.global_x * 0.4f;
            pending_y_ = pending_y_ * 0.6f + chosen.global_y * 0.4f;
            ++pending_hits_;
        }
        else
        {
            pending_x_ = chosen.global_x;
            pending_y_ = chosen.global_y;
            pending_hits_ = 1;
        }
    }
    else
    {
        has_pending_ = true;
        pending_x_ = chosen.global_x;
        pending_y_ = chosen.global_y;
        pending_hits_ = 1;
    }

    if (pending_hits_ >= settings_.acquire_confirm_frames)
    {
        has_lock_ = true;
        lock_x_ = pending_x_;
        lock_y_ = pending_y_;
        lock_lost_frames_ = 0;
        has_pending_ = false;
        pending_hits_ = 0;
        FillTarget(screen_center_x, screen_center_y, lock_x_, lock_y_, target);
        return true;
    }

    return false;
}

void StableTargetSelector::FillTarget(
    float screen_center_x,
    float screen_center_y,
    float target_x,
    float target_y,
    TargetPoint& out)
{
    const float dx = target_x - screen_center_x;
    const float dy = target_y - screen_center_y;
    out.x = target_x;
    out.y = target_y;
    out.distance = std::sqrt(dx * dx + dy * dy);
    out.valid = true;
}

bool StableTargetSelector::FindNearestToCenter(
    const std::vector<CandidateTarget>& candidates,
    CandidateTarget& out)
{
    if (candidates.empty())
    {
        return false;
    }

    float best_dist2 = std::numeric_limits<float>::max();
    for (const auto& c : candidates)
    {
        if (c.dist2_center < best_dist2)
        {
            best_dist2 = c.dist2_center;
            out = c;
        }
    }
    return best_dist2 < std::numeric_limits<float>::max();
}

bool StableTargetSelector::FindNearestToPoint(
    const std::vector<CandidateTarget>& candidates,
    float ref_x,
    float ref_y,
    float gate_px,
    CandidateTarget& out)
{
    const float gate2 = gate_px * gate_px;
    float best_dist2 = std::numeric_limits<float>::max();

    for (const auto& c : candidates)
    {
        const float dx = c.global_x - ref_x;
        const float dy = c.global_y - ref_y;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 > gate2)
        {
            continue;
        }
        if (dist2 < best_dist2)
        {
            best_dist2 = dist2;
            out = c;
        }
    }

    return best_dist2 < std::numeric_limits<float>::max();
}

void StableTargetSelector::HandleMiss()
{
    if (has_lock_)
    {
        ++lock_lost_frames_;
        if (lock_lost_frames_ > settings_.lost_tolerance_frames)
        {
            has_lock_ = false;
            lock_lost_frames_ = 0;
        }
    }

    has_pending_ = false;
    pending_hits_ = 0;
}

} // namespace aim

