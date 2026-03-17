#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

#include <opencv2/highgui.hpp>

#include "aim_control.h"
#include "app_config.h"
#include "control_panel.h"
#include "mouse_driver.h"
#include "ort_trt_infer.h"
#include "perf_logger.h"
#include "preview_renderer.h"
#include "runtime_helpers.h"
#include "runtime_tuning.h"
#include "screen_capture_dxgi.h"
#include "target_selector.h"
#include "yolo_decoder.h"

#pragma comment(lib, "winmm.lib")

namespace {

constexpr const char* kPreviewWindowName = "Detection";

const char* MoveAlgorithmName(aim::config::AimAlgorithm algorithm)
{
    switch (algorithm)
    {
    case aim::config::AimAlgorithm::Proportional:
        return "Proportional";
    case aim::config::AimAlgorithm::PID:
        return "PID";
    case aim::config::AimAlgorithm::Bezier:
        return "Bezier";
    case aim::config::AimAlgorithm::DirectRelative:
        return "DirectRelative";
    default:
        return "Unknown";
    }
}

} // namespace

int main()
{
    using Clock = std::chrono::steady_clock;

    aim::ConfigureConsoleUtf8();
    aim::EnableDpiAwareness();

    aim::RuntimeTuning runtime_tuning;
    runtime_tuning.aim_enabled = false;
    runtime_tuning.preview_enabled = aim::config::kShowPreviewWindow;
    runtime_tuning.verbose_log_enabled = aim::config::kEnableVerboseLog;

    ScreenCapturer capturer(aim::config::kCaptureSize, aim::config::kCaptureSize);
    const int screen_width = capturer.getWidth();
    const int screen_height = capturer.getHeight();
    const int capture_target_fps = std::max(1, aim::config::kCaptureTargetFps);
    const auto capture_interval = std::chrono::nanoseconds(1000000000LL / capture_target_fps);
    const double capture_interval_ms = std::chrono::duration<double, std::milli>(capture_interval).count();

    std::cout << "屏幕分辨率: " << screen_width << "x" << screen_height << "\n";
    std::cout << "截图后端: DXGI Desktop Duplication\n";
    std::cout << "截图节流: "
              << (aim::config::kLimitCaptureRate ? "开" : "关")
              << ", 目标FPS=" << capture_target_fps
              << ", 间隔(ms)=" << std::fixed << std::setprecision(2) << capture_interval_ms
              << ", 取帧超时(ms)=" << aim::config::kDxgiAcquireTimeoutMs << "\n"
              << std::defaultfloat;
    std::cout << "程序控制全部在 ImGui 面板完成（自瞄/可视化/日志/退出）。\n";

    const std::string model_path = aim::ResolveModelPath();
    std::cout << "模型路径: " << model_path << "\n";

    aim::OrtTrtInfer infer(aim::config::kCaptureSize);
    if (!infer.Initialize(model_path))
    {
        std::cerr << "TRT 推理后端初始化失败。\n";
        return 1;
    }
    std::cout << "推理后端: ONNX Runtime (" << infer.BackendName() << ")\n";

    MouseController mouse;
    if (!mouse.Initialize())
    {
        return 1;
    }
    std::cout << "移动算法: " << MoveAlgorithmName(aim::config::kAimAlgorithm)
              << ", 移动方式: " << (mouse.SupportsRelativeMove() ? "Relative(相对移动)" : "Absolute(绝对移动)")
              << "\n";

    aim::YoloDecoder decoder(runtime_tuning.conf_threshold, runtime_tuning.nms_iou_threshold);
    aim::AimControl aim_control(
        runtime_tuning.aim_smooth_factor,
        runtime_tuning.aim_max_step_px,
        runtime_tuning.aim_deadzone_px,
        aim::config::kCursorLockCenterThresholdPx);

    aim::StableTargetSelector target_selector;
    aim::TargetSelectorSettings selector_settings;
    selector_settings.active_radius_px = runtime_tuning.active_circle_radius_px;
    target_selector.SetSettings(selector_settings);

    aim::ControlPanel control_panel;
    if (!control_panel.Initialize(runtime_tuning))
    {
        std::cerr << "ImGui 控制面板初始化失败。\n";
        return 1;
    }
    control_panel.SetVisible(true);

    const int target_head_class_id = aim::HeadClassId();
    bool prev_aim_enabled = runtime_tuning.aim_enabled;
    bool prev_preview_enabled = runtime_tuning.preview_enabled;

    std::cout << "默认目标: " << aim::TargetClassName(target_head_class_id) << "\n";
    std::cout << "默认自瞄: " << (runtime_tuning.aim_enabled ? "开" : "关") << "\n";
    std::cout << "默认可视化: " << (runtime_tuning.preview_enabled ? "开" : "关") << "\n";

    aim::PerfLogger perf_logger;

    bool high_precision_timer_set = false;
    if (aim::config::kUseHighPrecisionTimer)
    {
        high_precision_timer_set = (timeBeginPeriod(1) == TIMERR_NOERROR);
        if (runtime_tuning.verbose_log_enabled)
        {
            std::cout << "[初始化] 高精度计时器: " << (high_precision_timer_set ? "开启" : "失败") << "\n";
        }
    }

    auto next_capture_tick = Clock::now();
    bool pace_started = false;

    while (true)
    {
        if (aim::config::kLimitCaptureRate)
        {
            if (!pace_started)
            {
                next_capture_tick = Clock::now();
                pace_started = true;
            }
            else
            {
                next_capture_tick += capture_interval;
                const auto now = Clock::now();
                if (now > next_capture_tick + capture_interval * 4)
                {
                    next_capture_tick = now;
                }
                aim::PreciseSleepUntil(next_capture_tick);
            }
        }

        control_panel.Poll(runtime_tuning);
        if (runtime_tuning.request_exit)
        {
            std::cout << "[控制面板] 退出\n";
            break;
        }

        if (runtime_tuning.preview_enabled != prev_preview_enabled)
        {
            if (!runtime_tuning.preview_enabled)
            {
                cv::destroyWindow(kPreviewWindowName);
            }
            prev_preview_enabled = runtime_tuning.preview_enabled;
        }

        if (runtime_tuning.aim_enabled != prev_aim_enabled)
        {
            target_selector.Reset();
            aim_control.Reset();
            prev_aim_enabled = runtime_tuning.aim_enabled;
            std::cout << "[控制面板] 自瞄已" << (runtime_tuning.aim_enabled ? "开启" : "关闭") << "\n";
        }

        decoder.SetThresholds(runtime_tuning.conf_threshold, runtime_tuning.nms_iou_threshold);
        aim_control.SetRuntimeParams(
            { runtime_tuning.aim_smooth_factor, runtime_tuning.aim_max_step_px, runtime_tuning.aim_deadzone_px });
        selector_settings.active_radius_px = runtime_tuning.active_circle_radius_px;
        target_selector.SetSettings(selector_settings);

        double capture_ms = 0.0;
        double infer_ms = 0.0;
        double decode_ms = 0.0;
        bool capture_ok = false;
        bool infer_ok = false;

        cv::Mat frame;
        aim::Detections detections;

        const auto cap_start = Clock::now();
        capture_ok = capturer.CaptureFrame(frame);
        capture_ms = std::chrono::duration<double, std::milli>(Clock::now() - cap_start).count();

        if (capture_ok)
        {
            aim::RawTensor raw_output;
            const auto infer_start = Clock::now();
            infer_ok = infer.Run(frame, raw_output);
            infer_ms = std::chrono::duration<double, std::milli>(Clock::now() - infer_start).count();

            if (infer_ok)
            {
                const auto decode_start = Clock::now();
                decoder.Decode(raw_output, detections);
                decode_ms = std::chrono::duration<double, std::milli>(Clock::now() - decode_start).count();

                if (runtime_tuning.aim_enabled)
                {
                    const float capture_offset_x = static_cast<float>(capturer.getWidth()) * 0.5f -
                        static_cast<float>(aim::config::kCaptureSize) * 0.5f;
                    const float capture_offset_y = static_cast<float>(capturer.getHeight()) * 0.5f -
                        static_cast<float>(aim::config::kCaptureSize) * 0.5f;
                    const float screen_center_x = static_cast<float>(screen_width) * 0.5f;
                    const float screen_center_y = static_cast<float>(screen_height) * 0.5f;

                    aim::TargetPoint target;
                    if (target_selector.Select(
                            detections,
                            capture_offset_x,
                            capture_offset_y,
                            screen_center_x,
                            screen_center_y,
                            target_head_class_id,
                            target))
                    {
                        target.x = std::clamp(target.x, 0.0f, static_cast<float>(screen_width - 1));
                        target.y = std::clamp(target.y, 0.0f, static_cast<float>(screen_height - 1));
                        aim_control.MoveToTarget(mouse, target.x, target.y, screen_width, screen_height);
                    }
                    else
                    {
                        aim_control.Reset();
                    }
                }

                if (runtime_tuning.preview_enabled)
                {
                    aim::DrawPreview(
                        frame,
                        detections,
                        runtime_tuning.aim_enabled,
                        target_head_class_id,
                        runtime_tuning.active_circle_radius_px);
                    cv::imshow(kPreviewWindowName, frame);
                }
            }
        }

        perf_logger.AddSample({ capture_ms, infer_ms, decode_ms, capture_ok, infer_ok });
        if (perf_logger.ShouldPrint(aim::config::kVerboseLogIntervalMs))
        {
            if (runtime_tuning.verbose_log_enabled)
            {
                perf_logger.PrintAndReset();
            }
            else
            {
                perf_logger.ResetWindow();
            }
        }

        if (runtime_tuning.preview_enabled)
        {
            cv::waitKey(1);
        }

        if (!aim::config::kLimitCaptureRate && !runtime_tuning.preview_enabled)
        {
            Sleep(aim::config::kLoopSleepMs);
        }
    }

    control_panel.Destroy();
    cv::destroyWindow(kPreviewWindowName);

    if (high_precision_timer_set)
    {
        timeEndPeriod(1);
    }

    return 0;
}

