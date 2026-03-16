#include "aim_control.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

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

void AimControl::MoveToTarget(
    MouseController& mouse,
    float target_x,
    float target_y,
    int screen_width,
    int screen_height) const
{
    const float screen_center_x = static_cast<float>(screen_width) * 0.5f;
    const float screen_center_y = static_cast<float>(screen_height) * 0.5f;

    float source_x = screen_center_x;
    float source_y = screen_center_y;
    bool use_relative_mode = false;

    POINT cursor = {};
    if (GetCursorPos(&cursor))
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
        return;
    }

    float move_dx = dx * smooth_factor_;
    float move_dy = dy * smooth_factor_;
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

        if (cmd_dx == 0 && std::fabs(dx) > deadzone_px_)
        {
            cmd_dx = (dx > 0.0f) ? 1 : -1;
        }
        if (cmd_dy == 0 && std::fabs(dy) > deadzone_px_)
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

} // namespace aim

