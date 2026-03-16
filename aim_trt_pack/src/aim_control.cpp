#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "aim_control.h"
#include <windows.h>

#include <algorithm>
#include <cmath>

namespace aim {

AimControl::AimControl(
    float smooth_factor,
    float max_step_px,
    float deadzone_px,
    float center_lock_threshold_px)
    : smooth_factor_(smooth_factor),
      max_step_px_(max_step_px),
      deadzone_px_(deadzone_px),
      center_lock_threshold_px_(center_lock_threshold_px)
{
}

void AimControl::Reset()
{
    pid_initialized_ = false;
    prev_error_x_ = 0.0f;
    prev_error_y_ = 0.0f;
    integral_x_ = 0.0f;
    integral_y_ = 0.0f;
    derivative_x_ = 0.0f;
    derivative_y_ = 0.0f;
    filtered_move_x_ = 0.0f;
    filtered_move_y_ = 0.0f;
    prev_cmd_dx_ = 0;
    prev_cmd_dy_ = 0;
    last_update_time_ = std::chrono::steady_clock::time_point{};
    last_debug_ = {};
}

void AimControl::MoveToTarget(
    MouseController& mouse,
    float target_x,
    float target_y,
    int screen_width,
    int screen_height)
{
    using Clock = std::chrono::steady_clock;

    last_debug_ = {};
    last_debug_.valid = true;
    last_debug_.target_x = target_x;
    last_debug_.target_y = target_y;

    const float screen_center_x = static_cast<float>(screen_width) * 0.5f;
    const float screen_center_y = static_cast<float>(screen_height) * 0.5f;

    float source_x = screen_center_x;
    float source_y = screen_center_y;
    bool use_relative_mode = false;

    POINT cursor = {};
    const bool has_cursor = GetCursorPos(&cursor);
    if (move_mode_ == config::AimMoveMode::Relative)
    {
        use_relative_mode = mouse.SupportsRelativeMove();
        if (!use_relative_mode && has_cursor)
        {
            source_x = static_cast<float>(cursor.x);
            source_y = static_cast<float>(cursor.y);
        }
    }
    else if (move_mode_ == config::AimMoveMode::Absolute)
    {
        use_relative_mode = false;
        if (has_cursor)
        {
            source_x = static_cast<float>(cursor.x);
            source_y = static_cast<float>(cursor.y);
        }
    }
    else if (has_cursor)
    {
        const float cursor_x = static_cast<float>(cursor.x);
        const float cursor_y = static_cast<float>(cursor.y);
        const bool cursor_locked_to_center =
            std::fabs(cursor_x - screen_center_x) <= center_lock_threshold_px_ &&
            std::fabs(cursor_y - screen_center_y) <= center_lock_threshold_px_;

        use_relative_mode = cursor_locked_to_center && mouse.SupportsRelativeMove();
        if (!use_relative_mode)
        {
            source_x = cursor_x;
            source_y = cursor_y;
        }
    }

    last_debug_.use_relative_mode = use_relative_mode;
    last_debug_.source_x = source_x;
    last_debug_.source_y = source_y;

    float dx = target_x - source_x;
    float dy = target_y - source_y;
    const float dist2 = dx * dx + dy * dy;
    last_debug_.error_x = dx;
    last_debug_.error_y = dy;
    last_debug_.error_dist = std::sqrt(dist2);
    if (dist2 <= deadzone_px_ * deadzone_px_)
    {
        // Decay controller memory near center to avoid oscillation.
        integral_x_ *= 0.7f;
        integral_y_ *= 0.7f;
        filtered_move_x_ *= 0.5f;
        filtered_move_y_ *= 0.5f;
        prev_cmd_dx_ = 0;
        prev_cmd_dy_ = 0;
        last_debug_.in_deadzone = true;
        last_debug_.move_filtered_x = filtered_move_x_;
        last_debug_.move_filtered_y = filtered_move_y_;
        return;
    }

    float move_dx = 0.0f;
    float move_dy = 0.0f;

    if (algorithm_ == config::AimAlgorithm::Proportional)
    {
        move_dx = dx * smooth_factor_;
        move_dy = dy * smooth_factor_;
    }
    else if (algorithm_ == config::AimAlgorithm::Bezier)
    {
        const float dist = std::sqrt(dist2);
        const float step_len = std::clamp(dist * smooth_factor_, 1.0f, max_step_px_);
        const float t_base = std::clamp(step_len / std::max(dist, 1e-3f), 0.02f, 0.45f);
        const float t = std::clamp(EaseOutCubic(t_base), 0.02f, 0.70f);
        const float omt = 1.0f - t;

        const float mid_x = (source_x + target_x) * 0.5f;
        const float mid_y = (source_y + target_y) * 0.5f;
        const float perp_x = -dy / dist;
        const float perp_y = dx / dist;

        const float bend_scale = std::clamp(bezier_strength_, 0.0f, 1.0f);
        const float bend_len = bend_scale * std::min(dist, bezier_distance_px_) * 0.30f;
        const float bend_sign = (dy >= 0.0f) ? 1.0f : -1.0f;
        const float ctrl_x = mid_x + perp_x * bend_len * bend_sign;
        const float ctrl_y = mid_y + perp_y * bend_len * bend_sign;

        const float bezier_x = omt * omt * source_x + 2.0f * omt * t * ctrl_x + t * t * target_x;
        const float bezier_y = omt * omt * source_y + 2.0f * omt * t * ctrl_y + t * t * target_y;

        move_dx = bezier_x - source_x;
        move_dy = bezier_y - source_y;
    }
    else
    {
        const auto now = Clock::now();
        float dt = 1.0f / 120.0f;
        if (pid_initialized_)
        {
            dt = std::chrono::duration<float>(now - last_update_time_).count();
            dt = std::clamp(dt, 0.001f, 0.05f);
        }
        else
        {
            pid_initialized_ = true;
        }
        last_update_time_ = now;
        last_debug_.dt_seconds = dt;

        integral_x_ += dx * dt;
        integral_y_ += dy * dt;
        integral_x_ = std::clamp(integral_x_, -pid_integral_limit_, pid_integral_limit_);
        integral_y_ = std::clamp(integral_y_, -pid_integral_limit_, pid_integral_limit_);

        const float raw_derivative_x = (dx - prev_error_x_) / dt;
        const float raw_derivative_y = (dy - prev_error_y_) / dt;
        derivative_x_ =
            pid_derivative_alpha_ * derivative_x_ + (1.0f - pid_derivative_alpha_) * raw_derivative_x;
        derivative_y_ =
            pid_derivative_alpha_ * derivative_y_ + (1.0f - pid_derivative_alpha_) * raw_derivative_y;

        prev_error_x_ = dx;
        prev_error_y_ = dy;

        move_dx = smooth_factor_ * dx + pid_ki_ * integral_x_ + pid_kd_ * derivative_x_;
        move_dy = smooth_factor_ * dy + pid_ki_ * integral_y_ + pid_kd_ * derivative_y_;
    }

    last_debug_.move_raw_x = move_dx;
    last_debug_.move_raw_y = move_dy;

    // Additional damping near center to suppress jitter.
    const float dist = std::sqrt(dist2);
    if (dist < near_zone_px_)
    {
        const float ratio = std::clamp(dist / std::max(near_zone_px_, 1e-3f), 0.0f, 1.0f);
        const float gain =
            near_zone_min_gain_ + (1.0f - near_zone_min_gain_) * ratio;
        move_dx *= gain;
        move_dy *= gain;
    }
    last_debug_.move_post_gain_x = move_dx;
    last_debug_.move_post_gain_y = move_dy;

    // Low-pass output command to smooth per-frame target noise.
    float alpha = std::clamp(output_lpf_alpha_, 0.0f, 0.98f);
    if (dist < near_zone_px_)
    {
        alpha = std::min(alpha, std::clamp(near_zone_lpf_alpha_, 0.0f, 0.98f));
    }
    const float beta = 1.0f - alpha;
    filtered_move_x_ = filtered_move_x_ * alpha + move_dx * beta;
    filtered_move_y_ = filtered_move_y_ * alpha + move_dy * beta;

    // If output direction is opposite to current error, quickly decay residual tail.
    const float sign_flip_damp = std::clamp(sign_flip_damping_, 0.0f, 1.0f);
    if (filtered_move_x_ * dx < 0.0f)
    {
        filtered_move_x_ *= sign_flip_damp;
    }
    if (filtered_move_y_ * dy < 0.0f)
    {
        filtered_move_y_ *= sign_flip_damp;
    }

    move_dx = filtered_move_x_;
    move_dy = filtered_move_y_;

    // Hard clamp: command cannot exceed current error proportion (anti-overshoot).
    const float overshoot_ratio = std::clamp(output_overshoot_ratio_, 0.05f, 1.20f);
    const float max_follow_x = std::fabs(dx) * overshoot_ratio;
    const float max_follow_y = std::fabs(dy) * overshoot_ratio;
    move_dx = std::clamp(move_dx, -max_follow_x, max_follow_x);
    move_dy = std::clamp(move_dy, -max_follow_y, max_follow_y);

    float move_len = std::sqrt(move_dx * move_dx + move_dy * move_dy);
    if (dist < near_zone_px_)
    {
        const float near_cmd_ratio = std::clamp(near_zone_max_cmd_ratio_, 0.05f, 1.0f);
        const float near_cmd_cap = std::max(1.0f, dist * near_cmd_ratio);
        if (move_len > near_cmd_cap)
        {
            const float scale = near_cmd_cap / std::max(move_len, 1e-3f);
            move_dx *= scale;
            move_dy *= scale;
            move_len = near_cmd_cap;
        }
    }

    if (move_len > max_step_px_)
    {
        const float scale = max_step_px_ / move_len;
        move_dx *= scale;
        move_dy *= scale;
    }

    // Keep internal state consistent with post-clamp output.
    filtered_move_x_ = move_dx;
    filtered_move_y_ = move_dy;

    last_debug_.move_filtered_x = move_dx;
    last_debug_.move_filtered_y = move_dy;

    if (use_relative_mode)
    {
        int cmd_dx = static_cast<int>(std::lround(move_dx));
        int cmd_dy = static_cast<int>(std::lround(move_dy));

        if (dist < micro_error_px_)
        {
            if (std::fabs(move_dx) < micro_move_deadband_px_)
            {
                cmd_dx = 0;
            }
            if (std::fabs(move_dy) < micro_move_deadband_px_)
            {
                cmd_dy = 0;
            }
        }

        if (dist < flip_suppress_error_px_)
        {
            const int cmd_limit = std::max(1, flip_suppress_cmd_px_);
            if (cmd_dx != 0 && prev_cmd_dx_ != 0 &&
                std::abs(cmd_dx) <= cmd_limit && std::abs(prev_cmd_dx_) <= cmd_limit &&
                ((cmd_dx > 0) != (prev_cmd_dx_ > 0)))
            {
                cmd_dx = 0;
            }
            if (cmd_dy != 0 && prev_cmd_dy_ != 0 &&
                std::abs(cmd_dy) <= cmd_limit && std::abs(prev_cmd_dy_) <= cmd_limit &&
                ((cmd_dy > 0) != (prev_cmd_dy_ > 0)))
            {
                cmd_dy = 0;
            }
        }

        if (cmd_dx == 0 && std::fabs(dx) > relative_min_step_error_px_ && std::fabs(move_dx) > 0.2f)
        {
            cmd_dx = (dx > 0.0f) ? 1 : -1;
        }
        if (cmd_dy == 0 && std::fabs(dy) > relative_min_step_error_px_ && std::fabs(move_dy) > 0.2f)
        {
            cmd_dy = (dy > 0.0f) ? 1 : -1;
        }

        last_debug_.cmd_dx = cmd_dx;
        last_debug_.cmd_dy = cmd_dy;

        if (cmd_dx != 0 || cmd_dy != 0)
        {
            mouse.MoveRelative(cmd_dx, cmd_dy);
        }

        prev_cmd_dx_ = cmd_dx;
        prev_cmd_dy_ = cmd_dy;
        return;
    }

    const float next_x = std::clamp(source_x + move_dx, 0.0f, static_cast<float>(screen_width - 1));
    const float next_y = std::clamp(source_y + move_dy, 0.0f, static_cast<float>(screen_height - 1));
    last_debug_.cmd_dx = static_cast<int>(std::lround(next_x - source_x));
    last_debug_.cmd_dy = static_cast<int>(std::lround(next_y - source_y));
    prev_cmd_dx_ = last_debug_.cmd_dx;
    prev_cmd_dy_ = last_debug_.cmd_dy;
    mouse.MoveTo(ToDllCoord(next_x, screen_width), ToDllCoord(next_y, screen_height));
}

unsigned short AimControl::ToDllCoord(float pixel_value, int pixel_max)
{
    const float clamped = std::clamp(pixel_value, 0.0f, static_cast<float>(pixel_max - 1));
    return static_cast<unsigned short>(std::lround(clamped));
}

float AimControl::EaseOutCubic(float x)
{
    const float t = std::clamp(x, 0.0f, 1.0f);
    const float one_minus = 1.0f - t;
    return 1.0f - one_minus * one_minus * one_minus;
}

} // namespace aim
