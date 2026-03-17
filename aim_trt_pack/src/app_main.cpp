#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <fstream>
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

struct CsvLogState {
    std::ofstream file;
    int pending_flush = 0;
};

CsvLogState OpenTuningCsvIfEnabled()
{
    CsvLogState state;
    if (!aim::config::kEnableTuningCsvLog)
    {
        return state;
    }

    if (!aim::EnsureLogsDirectory())
    {
        std::cerr << "调参日志目录创建失败: logs\n";
        return state;
    }

    const std::string csv_path = aim::BuildTuningCsvPath();
    state.file.open(csv_path, std::ios::out | std::ios::trunc);
    if (!state.file.is_open())
    {
        std::cerr << "调参日志创建失败: " << csv_path << "\n";
        return state;
    }

    state.file << "t_ms,frame,aim,preview,cap_ok,infer_ok,det_count,target_locked,target_dist_px,"
                  "target_x,target_y,error_x,error_y,error_dist,move_raw_x,move_raw_y,"
                  "move_post_gain_x,move_post_gain_y,move_filtered_x,move_filtered_y,"
                  "cmd_dx,cmd_dy,use_relative,deadzone,dt_s,cap_ms,infer_ms,decode_ms,total_ms\n";
    std::cout << "调参日志: " << csv_path << "\n";
    return state;
}

void WriteCsvRow(
    CsvLogState& csv,
    long long frame_index,
    bool aim_enabled,
    bool preview_enabled,
    bool capture_ok,
    bool infer_ok,
    const aim::Detections& detections,
    bool target_locked,
    bool has_target_for_log,
    const aim::TargetPoint& selected_target,
    const aim::AimDebugSnapshot& dbg,
    double capture_ms,
    double infer_ms,
    double decode_ms,
    std::chrono::steady_clock::time_point app_start_time)
{
    if (!csv.file.is_open())
    {
        return;
    }

    if (aim::config::kTuningCsvLogOnlyAimEnabled && !aim_enabled)
    {
        return;
    }

    const double total_ms = capture_ms + infer_ms + decode_ms;
    const double t_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - app_start_time).count();

    csv.file << std::fixed << std::setprecision(3)
             << t_ms << ","
             << frame_index << ","
             << (aim_enabled ? 1 : 0) << ","
             << (preview_enabled ? 1 : 0) << ","
             << (capture_ok ? 1 : 0) << ","
             << (infer_ok ? 1 : 0) << ","
             << detections.size() << ","
             << (target_locked ? 1 : 0) << ","
             << (has_target_for_log ? selected_target.distance : 0.0f) << ","
             << (has_target_for_log ? selected_target.x : 0.0f) << ","
             << (has_target_for_log ? selected_target.y : 0.0f) << ","
             << dbg.error_x << ","
             << dbg.error_y << ","
             << dbg.error_dist << ","
             << dbg.move_raw_x << ","
             << dbg.move_raw_y << ","
             << dbg.move_post_gain_x << ","
             << dbg.move_post_gain_y << ","
             << dbg.move_filtered_x << ","
             << dbg.move_filtered_y << ","
             << dbg.cmd_dx << ","
             << dbg.cmd_dy << ","
             << (dbg.use_relative_mode ? 1 : 0) << ","
             << (dbg.in_deadzone ? 1 : 0) << ","
             << dbg.dt_seconds << ","
             << capture_ms << ","
             << infer_ms << ","
             << decode_ms << ","
             << total_ms
             << "\n";

    ++csv.pending_flush;
    if (csv.pending_flush >= std::max(1, aim::config::kTuningCsvFlushIntervalFrames))
    {
        csv.file.flush();
        csv.pending_flush = 0;
    }
}

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
    std::cout << "全局热键: [Q]=开启自瞄, [K]=关闭自瞄, [V]=可视化开关, [B]=控制面板开关, [F6]=退出\n";

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
    control_panel.Initialize(runtime_tuning);
    control_panel.SetVisible(aim::config::kShowControlPanel);

    bool aim_enabled = false;
    int target_head_class_id = aim::HeadClassId();
    bool prev_q = false;
    bool prev_k = false;
    bool prev_v = false;
    bool prev_b = false;
    bool prev_exit = false;

    std::cout << "默认目标: " << aim::TargetClassName(target_head_class_id) << "\n";
    std::cout << "默认可视化: " << (runtime_tuning.preview_enabled ? "开" : "关") << "\n";
    std::cout << "默认控制面板: " << (control_panel.IsVisible() ? "开" : "关") << "\n";

    CsvLogState tuning_csv = OpenTuningCsvIfEnabled();
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
    long long frame_index = 0;
    const auto app_start_time = Clock::now();

    while (true)
    {
        ++frame_index;

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
        bool target_locked = false;
        bool has_target_for_log = false;
        aim::TargetPoint selected_target;
        aim::AimDebugSnapshot dbg = {};

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

                if (aim_enabled)
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
                        selected_target = target;
                        has_target_for_log = true;

                        aim_control.MoveToTarget(mouse, target.x, target.y, screen_width, screen_height);
                        target_locked = true;
                        dbg = aim_control.LastDebug();
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
                        aim_enabled,
                        target_head_class_id,
                        runtime_tuning.active_circle_radius_px);
                    cv::imshow(kPreviewWindowName, frame);
                }
            }
        }

        perf_logger.AddSample({ capture_ms, infer_ms, decode_ms, capture_ok, infer_ok });

        if (aim::KeyPressedEdge(aim::config::kHotkeyEnableAim, prev_q))
        {
            aim_enabled = true;
            target_selector.Reset();
            aim_control.Reset();
            std::cout << "[热键] 自瞄已开启\n";
        }
        if (aim::KeyPressedEdge(aim::config::kHotkeyDisableAim, prev_k))
        {
            aim_enabled = false;
            target_selector.Reset();
            aim_control.Reset();
            std::cout << "[热键] 自瞄已关闭\n";
        }
        if (aim::KeyPressedEdge(aim::config::kHotkeyTogglePreview, prev_v))
        {
            runtime_tuning.preview_enabled = !runtime_tuning.preview_enabled;
            control_panel.SyncFromRuntime(runtime_tuning);
            if (!runtime_tuning.preview_enabled)
            {
                cv::destroyWindow(kPreviewWindowName);
            }
            std::cout << "[热键] 可视化已" << (runtime_tuning.preview_enabled ? "开启" : "关闭") << "\n";
        }
        if (aim::KeyPressedEdge(aim::config::kHotkeyToggleControlPanel, prev_b))
        {
            control_panel.SetVisible(!control_panel.IsVisible());
            std::cout << "[热键] 控制面板已" << (control_panel.IsVisible() ? "开启" : "关闭") << "\n";
        }
        if (aim::KeyPressedEdge(aim::config::kHotkeyExit, prev_exit))
        {
            std::cout << "[热键] 退出\n";
            break;
        }

        WriteCsvRow(
            tuning_csv,
            frame_index,
            aim_enabled,
            runtime_tuning.preview_enabled,
            capture_ok,
            infer_ok,
            detections,
            target_locked,
            has_target_for_log,
            selected_target,
            dbg,
            capture_ms,
            infer_ms,
            decode_ms,
            app_start_time);

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

        if (!aim::config::kLimitCaptureRate && !runtime_tuning.preview_enabled && !control_panel.IsVisible())
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

    if (tuning_csv.file.is_open())
    {
        tuning_csv.file.flush();
        tuning_csv.file.close();
    }

    return 0;
}
