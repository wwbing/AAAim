#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

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
    try
    {
        using Clock = std::chrono::steady_clock;

        aim::ConfigureConsoleUtf8();
        std::cout << "[启动] aim_stable 启动中..." << std::endl;
        aim::EnableDpiAwareness();

        aim::RuntimeTuning runtime_tuning;
        runtime_tuning.aim_enabled = false;
        runtime_tuning.preview_enabled = aim::config::kShowPreviewWindow;
        runtime_tuning.verbose_log_enabled = aim::config::kEnableVerboseLog;

        aim::ControlPanel control_panel;
        if (!control_panel.Initialize(runtime_tuning))
        {
            std::cerr << "ImGui 控制面板初始化失败。" << std::endl;
            return 1;
        }
        control_panel.SetVisible(aim::config::kShowControlPanel);
        control_panel.Poll(runtime_tuning);

        std::cout << "[启动] 正在初始化截图模块..." << std::endl;
        ScreenCapturer capturer(aim::config::kCaptureSize, aim::config::kCaptureSize);
        std::cout << "[启动] 截图后端: "
                  << (capturer.isUsingGdiFallback() ? "GDI(兼容模式)" : "DXGI")
                  << std::endl;

        const int screen_width = capturer.getWidth();
        const int screen_height = capturer.getHeight();
        const int capture_target_fps = std::max(1, aim::config::kCaptureTargetFps);
        const auto capture_interval = std::chrono::nanoseconds(1000000000LL / capture_target_fps);
        const double capture_interval_ms = std::chrono::duration<double, std::milli>(capture_interval).count();

        std::cout << "屏幕分辨率: " << screen_width << "x" << screen_height << "\n";
        std::cout << "截图节流: "
                  << (aim::config::kLimitCaptureRate ? "开启" : "关闭")
                  << ", 目标FPS=" << capture_target_fps
                  << ", 间隔(ms)=" << std::fixed << std::setprecision(2) << capture_interval_ms
                  << ", 取帧超时(ms)=" << aim::config::kDxgiAcquireTimeoutMs << "\n"
                  << std::defaultfloat;
        std::cout << "控制方式: 使用 ImGui 面板实时调参（自瞄/可视化/日志/退出）。\n";

        const std::string model_path = aim::ResolveModelPath();
        std::cout << "模型路径: " << model_path << "\n";

        aim::OrtTrtInfer infer(aim::config::kCaptureSize);
        const bool prefer_trt = aim::config::kPreferTensorRt;
        std::cout << "[启动] 正在初始化推理会话（"
                  << (prefer_trt ? "优先TensorRT，失败回退CUDA/CPU" : "优先CUDA，必要时回退CPU")
                  << "）..." << std::endl;
        runtime_tuning.init_in_progress = true;
        runtime_tuning.init_failed = false;
        runtime_tuning.init_progress = -1.0f;
        runtime_tuning.init_status_text = prefer_trt
            ? "准备初始化推理会话（TensorRT优先）..."
            : "准备初始化推理会话（CUDA优先）...";
        control_panel.SyncFromRuntime(runtime_tuning);
        control_panel.Poll(runtime_tuning);

        std::atomic<bool> infer_init_done{ false };
        std::atomic<bool> infer_init_ok{ false };
        std::thread infer_init_thread([&]() {
            infer_init_ok.store(infer.Initialize(model_path), std::memory_order_relaxed);
            infer_init_done.store(true, std::memory_order_release);
        });

        const auto init_begin = Clock::now();
        auto last_wait_log = Clock::now();
        bool slow_warn_printed = false;
        while (!infer_init_done.load(std::memory_order_acquire))
        {
            runtime_tuning.init_progress = -1.0f;
            runtime_tuning.init_status_text = prefer_trt
                ? "正在创建推理会话（TensorRT首次会构建引擎，可能较慢）..."
                : "正在创建推理会话（CUDA）...";
            control_panel.SyncFromRuntime(runtime_tuning);
            control_panel.Poll(runtime_tuning);
            const auto now = Clock::now();
            if (now - last_wait_log >= std::chrono::seconds(5))
            {
                std::cout << "[启动] 推理会话仍在初始化，请稍候..." << std::endl;
                last_wait_log = now;
            }
            if (prefer_trt &&
                !slow_warn_printed &&
                now - init_begin >= std::chrono::seconds(aim::config::kInitSlowWarnSeconds))
            {
                slow_warn_printed = true;
                runtime_tuning.init_status_text =
                    "TensorRT初始化明显偏慢，建议先切换CUDA（kPreferTensorRt=false）确认可运行。";
                std::cerr << "[警告] TensorRT 初始化超过 "
                          << aim::config::kInitSlowWarnSeconds
                          << " 秒，当前模型首轮构建可能非常慢。若需快速启动，建议改用 CUDA 后端。"
                          << std::endl;
            }
            Sleep(16);
        }
        infer_init_thread.join();

        if (!infer_init_ok.load(std::memory_order_relaxed))
        {
            std::cerr << "推理会话初始化失败（TensorRT/CUDA/CPU 均不可用）。" << std::endl;
            runtime_tuning.init_in_progress = false;
            runtime_tuning.init_failed = true;
            runtime_tuning.init_progress = 1.0f;
            runtime_tuning.init_status_text = "推理会话初始化失败，请检查模型文件与ORT/CUDA/TRT运行库。";
            control_panel.SyncFromRuntime(runtime_tuning);
            control_panel.Poll(runtime_tuning);
            control_panel.Destroy();
            return 1;
        }
        runtime_tuning.init_in_progress = false;
        runtime_tuning.init_failed = false;
        runtime_tuning.init_progress = 1.0f;
        runtime_tuning.init_status_text = "推理会话初始化完成。";
        control_panel.SyncFromRuntime(runtime_tuning);
        std::cout << "推理后端: ONNX Runtime (" << infer.BackendName() << ")\n";

        MouseController mouse;
        if (!mouse.Initialize())
        {
            control_panel.Destroy();
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

        const int target_head_class_id = aim::HeadClassId();
        bool prev_aim_enabled = runtime_tuning.aim_enabled;
        bool prev_preview_enabled = runtime_tuning.preview_enabled;

        std::cout << "默认目标: " << aim::TargetClassName(target_head_class_id) << "\n";
        std::cout << "默认自瞄: " << (runtime_tuning.aim_enabled ? "开启" : "关闭") << "\n";
        std::cout << "默认可视化: " << (runtime_tuning.preview_enabled ? "开启" : "关闭") << "\n";

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
                std::cout << "[控制面板] 请求退出\n";
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
                std::cout << "[控制面板] 自瞄"
                          << (runtime_tuning.aim_enabled ? "已开启" : "已关闭")
                          << "\n";
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
    catch (const std::exception& ex)
    {
        std::cerr << "[致命错误] 未捕获异常: " << ex.what() << std::endl;
        MessageBoxA(nullptr, ex.what(), "aim_stable fatal error", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return 1;
    }
    catch (...)
    {
        std::cerr << "[致命错误] 未捕获未知异常。" << std::endl;
        MessageBoxA(nullptr, "Unknown exception", "aim_stable fatal error", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return 1;
    }
}
