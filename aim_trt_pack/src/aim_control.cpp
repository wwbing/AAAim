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
    last_update_time_ = std::chrono::steady_clock::time_point{};
}

void AimControl::MoveToTarget(
    MouseController& mouse,
    float target_x,
    float target_y,
    int screen_width,
    int screen_height)
{
    using Clock = std::chrono::steady_clock;

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

    float dx = target_x - source_x;
    float dy = target_y - source_y;
    const float dist2 = dx * dx + dy * dy;
    if (dist2 <= deadzone_px_ * deadzone_px_)
    {
        // Decay controller memory near center to avoid oscillation.
        integral_x_ *= 0.7f;
        integral_y_ *= 0.7f;
        filtered_move_x_ *= 0.5f;
        filtered_move_y_ *= 0.5f;
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

    // Low-pass output command to smooth per-frame target noise.
    const float alpha = std::clamp(output_lpf_alpha_, 0.0f, 0.98f);
    const float beta = 1.0f - alpha;
    filtered_move_x_ = filtered_move_x_ * alpha + move_dx * beta;
    filtered_move_y_ = filtered_move_y_ * alpha + move_dy * beta;
    move_dx = filtered_move_x_;
    move_dy = filtered_move_y_;

    const float move_len = std::sqrt(move_dx * move_dx + move_dy * move_dy);
    if (move_len > max_step_px_)
    {
        const float scale = max_step_px_ / move_len;
        move_dx *= scale;
        move_dy *= scale;
    }

    if (use_relative_mode)
    {
        int cmd_dx = static_cast<int>(std::lround(move_dx));
        int cmd_dy = static_cast<int>(std::lround(move_dy));

        if (cmd_dx == 0 && std::fabs(dx) > relative_min_step_error_px_ && std::fabs(move_dx) > 0.2f)
        {
            cmd_dx = (dx > 0.0f) ? 1 : -1;
        }
        if (cmd_dy == 0 && std::fabs(dy) > relative_min_step_error_px_ && std::fabs(move_dy) > 0.2f)
        {
            cmd_dy = (dy > 0.0f) ? 1 : -1;
        }

        if (cmd_dx != 0 || cmd_dy != 0)
        {
            mouse.MoveRelative(cmd_dx, cmd_dy);
        }
        return;
    }

    const float next_x = std::clamp(source_x + move_dx, 0.0f, static_cast<float>(screen_width - 1));
    const float next_y = std::clamp(source_y + move_dy, 0.0f, static_cast<float>(screen_height - 1));
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
