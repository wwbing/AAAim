#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include "aim_control.h"
#include "app_config.h"
#include "mouse_driver.h"
#include "ort_trt_infer.h"
#include "screen_capture_dxgi.h"
#include "yolo_decoder.h"

#pragma comment(lib, "winmm.lib")

namespace {

constexpr const char* kPreviewWindowName = "Detection";
constexpr int kClassHead = 0;

const std::vector<std::string> kClassNames = { "head" };

const char* TargetClassName(int target_class_id)
{
    return target_class_id == kClassHead ? "HEAD" : "UNKNOWN";
}

cv::Scalar ClassColor(int cls_idx)
{
    // BGR color for single-class head model.
    switch (cls_idx)
    {
    case 0: // head
        return cv::Scalar(0, 255, 0);     // green
    default:
        return cv::Scalar(180, 180, 180); // gray fallback
    }
}

bool FileExists(const std::string& path)
{
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string DirnameOf(const std::string& path)
{
    const size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
    {
        return ".";
    }
    return path.substr(0, pos);
}

std::string BuildTuningCsvPath()
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char filename[128] = { 0 };
    std::snprintf(
        filename,
        sizeof(filename),
        "logs\\aim_debug_%04d%02d%02d_%02d%02d%02d.csv",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond);
    return std::string(filename);
}

bool EnsureLogsDirectory()
{
    if (CreateDirectoryA("logs", nullptr) != 0)
    {
        return true;
    }
    const DWORD err = GetLastError();
    return err == ERROR_ALREADY_EXISTS;
}

std::string ResolveModelPath()
{
    std::vector<std::string> candidates = {
        "models\\CS2_1_CLS_Sim.onnx",
        "CS2_1_CLS_Sim.onnx",
        "models\\CS2_4_CLS_Sim.onnx",
        "CS2_4_CLS_Sim.onnx",
        "models\\aimlab_blueball_sim.onnx",
        "aimlab_blueball_sim.onnx",
        "models\\best-sim.onnx",
        "best-sim.onnx"
    };

    char exe_path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0)
    {
        const std::string exe_dir = DirnameOf(std::string(exe_path));
        candidates.push_back(exe_dir + "\\CS2_1_CLS_Sim.onnx");
        candidates.push_back(exe_dir + "\\CS2_4_CLS_Sim.onnx");
        candidates.push_back(exe_dir + "\\aimlab_blueball_sim.onnx");
        candidates.push_back(exe_dir + "\\best-sim.onnx");
        candidates.push_back(exe_dir + "\\models\\CS2_1_CLS_Sim.onnx");
        candidates.push_back(exe_dir + "\\models\\CS2_4_CLS_Sim.onnx");
        candidates.push_back(exe_dir + "\\models\\aimlab_blueball_sim.onnx");
        candidates.push_back(exe_dir + "\\models\\best-sim.onnx");
        candidates.push_back(exe_dir + "\\..\\..\\models\\CS2_1_CLS_Sim.onnx");
        candidates.push_back(exe_dir + "\\..\\..\\models\\CS2_4_CLS_Sim.onnx");
        candidates.push_back(exe_dir + "\\..\\..\\models\\aimlab_blueball_sim.onnx");
        candidates.push_back(exe_dir + "\\..\\..\\models\\best-sim.onnx");
    }

    for (const auto& p : candidates)
    {
        if (FileExists(p))
        {
            return p;
        }
    }

    return "models\\CS2_1_CLS_Sim.onnx";
}

bool KeyPressedEdge(int vk, bool& prev_down)
{
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool edge = down && !prev_down;
    prev_down = down;
    return edge;
}

void EnableDpiAwareness()
{
    SetProcessDPIAware();
}

void ConfigureConsoleUtf8()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
}

template<typename ClockT>
void PreciseSleepUntil(const std::chrono::time_point<ClockT>& target_time)
{
    while (true)
    {
        const auto now = ClockT::now();
        if (now >= target_time)
        {
            return;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(target_time - now);
        if (remaining.count() > 2)
        {
            Sleep(1);
        }
        else
        {
            SwitchToThread();
        }
    }
}

void DrawPreview(cv::Mat& frame, const aim::Detections& detections, bool aim_enabled, int target_class_id)
{
    const int cx = frame.cols / 2;
    const int cy = frame.rows / 2;
    const int radius = static_cast<int>(std::lround(std::max(1.0f, aim::config::kAimActiveCircleRadiusPx)));
    cv::circle(frame, cv::Point(cx, cy), radius, cv::Scalar(0, 255, 255), 1);

    for (const auto& det : detections)
    {
        const cv::Point p1(static_cast<int>(det[0]), static_cast<int>(det[1]));
        const cv::Point p2(static_cast<int>(det[2]), static_cast<int>(det[3]));
        const int cls_idx = static_cast<int>(det[5]);
        const cv::Scalar cls_color = ClassColor(cls_idx);
        const int thickness = (cls_idx == target_class_id) ? 3 : 2;
        cv::rectangle(frame, p1, p2, cls_color, thickness);
        const std::string cls_name =
            (cls_idx >= 0 && cls_idx < static_cast<int>(kClassNames.size())) ? kClassNames[cls_idx] : "obj";
        cv::putText(
            frame,
            cls_name + " " + std::to_string(det[4]),
            p1,
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cls_color,
            1);
    }

    const std::string status = std::string("自瞄: ") + (aim_enabled ? "开" : "关") +
        " | 目标: " + TargetClassName(target_class_id);
    cv::putText(
        frame,
        status,
        cv::Point(10, 24),
        cv::FONT_HERSHEY_SIMPLEX,
        0.6,
        cv::Scalar(0, 255, 255),
        2);

    // 类别图例
    int legend_y = 48;
    for (int cls_idx = 0; cls_idx < static_cast<int>(kClassNames.size()); ++cls_idx)
    {
        const cv::Scalar cls_color = ClassColor(cls_idx);
        cv::rectangle(frame, cv::Point(10, legend_y - 12), cv::Point(28, legend_y + 4), cls_color, cv::FILLED);
        cv::putText(
            frame,
            kClassNames[cls_idx],
            cv::Point(34, legend_y),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cls_color,
            1);
        legend_y += 20;
    }
}

struct CandidateTarget {
    float global_x = 0.0f;
    float global_y = 0.0f;
    float dist2_center = 0.0f;
};

class StableTargetSelector {
public:
    void Reset()
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

    bool Select(
        const aim::Detections& detections,
        float capture_offset_x,
        float capture_offset_y,
        float screen_center_x,
        float screen_center_y,
        int target_class_id,
        aim::TargetPoint& target)
    {
        target = {};

        const float active_radius = std::max(1.0f, aim::config::kAimActiveCircleRadiusPx);
        const float active_radius2 = active_radius * active_radius;

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

            // Hard filter: only keep targets inside center active circle.
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
            if (FindNearestToPoint(candidates, lock_x_, lock_y_, aim::config::kTargetTrackGatePx, chosen))
            {
                const float keep = std::clamp(aim::config::kTargetTrackSmoothing, 0.0f, 0.98f);
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
            if (dist <= aim::config::kTargetAcquireGatePx)
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

        if (pending_hits_ >= std::max(1, aim::config::kTargetAcquireConfirmFrames))
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

private:
    static void FillTarget(
        float screen_center_x,
        float screen_center_y,
        float target_x,
        float target_y,
        aim::TargetPoint& out)
    {
        const float dx = target_x - screen_center_x;
        const float dy = target_y - screen_center_y;
        out.x = target_x;
        out.y = target_y;
        out.distance = std::sqrt(dx * dx + dy * dy);
        out.valid = true;
    }

    static bool FindNearestToCenter(const std::vector<CandidateTarget>& candidates, CandidateTarget& out)
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

    static bool FindNearestToPoint(
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

    void HandleMiss()
    {
        if (has_lock_)
        {
            ++lock_lost_frames_;
            if (lock_lost_frames_ > std::max(0, aim::config::kTargetLostToleranceFrames))
            {
                has_lock_ = false;
                lock_lost_frames_ = 0;
            }
        }
        has_pending_ = false;
        pending_hits_ = 0;
    }

private:
    bool has_lock_ = false;
    float lock_x_ = 0.0f;
    float lock_y_ = 0.0f;
    int lock_lost_frames_ = 0;

    bool has_pending_ = false;
    float pending_x_ = 0.0f;
    float pending_y_ = 0.0f;
    int pending_hits_ = 0;
};

} // namespace

int main()
{
    ConfigureConsoleUtf8();
    EnableDpiAwareness();

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
    std::cout << "全局热键: [Q]=开启自瞄, [K]=关闭自瞄, [V]=可视化开关, [F6]=退出\n";

    const std::string model_path = ResolveModelPath();
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
    std::cout << "移动算法: DirectRelative, 移动方式: "
              << (mouse.SupportsRelativeMove() ? "Relative(相对移动)" : "Absolute(绝对移动)")
              << "\n";

    aim::YoloDecoder decoder(aim::config::kConfThreshold, aim::config::kNmsIouThreshold);
    aim::AimControl aim_control(
        aim::config::kAimSmoothFactor,
        aim::config::kAimMaxStepPx,
        aim::config::kAimDeadzonePx,
        aim::config::kCursorLockCenterThresholdPx);
    StableTargetSelector target_selector;

    bool aim_enabled = false;
    bool preview_enabled = aim::config::kShowPreviewWindow;
    int target_head_class_id = kClassHead;
    bool prev_q = false;
    bool prev_k = false;
    bool prev_v = false;
    bool prev_esc = false;
    std::cout << "默认目标: " << TargetClassName(target_head_class_id) << "\n";
    std::cout << "默认可视化: " << (preview_enabled ? "开" : "关") << "\n";

    std::ofstream tuning_csv;
    std::string tuning_csv_path;
    int tuning_csv_pending_flush = 0;
    if (aim::config::kEnableTuningCsvLog)
    {
        if (EnsureLogsDirectory())
        {
            tuning_csv_path = BuildTuningCsvPath();
            tuning_csv.open(tuning_csv_path, std::ios::out | std::ios::trunc);
            if (tuning_csv.is_open())
            {
                tuning_csv << "t_ms,frame,aim,preview,cap_ok,infer_ok,det_count,target_locked,target_dist_px,"
                              "target_x,target_y,error_x,error_y,error_dist,move_raw_x,move_raw_y,"
                              "move_post_gain_x,move_post_gain_y,move_filtered_x,move_filtered_y,"
                              "cmd_dx,cmd_dy,use_relative,deadzone,dt_s,cap_ms,infer_ms,decode_ms,total_ms\n";
                std::cout << "调参日志: " << tuning_csv_path << "\n";
            }
            else
            {
                std::cerr << "调参日志创建失败: " << tuning_csv_path << "\n";
            }
        }
        else
        {
            std::cerr << "调参日志目录创建失败: logs\n";
        }
    }

    using Clock = std::chrono::steady_clock;
    const auto app_start_time = Clock::now();
    auto stats_window_start = Clock::now();
    int stats_frames = 0;
    int stats_capture_ok = 0;
    int stats_infer_ok = 0;
    int stats_target_locks = 0;
    int stats_last_detections = 0;
    int stats_infer_attempts = 0;
    int stats_decode_count = 0;
    double stats_capture_ms_sum = 0.0;
    double stats_infer_ms_sum = 0.0;
    double stats_decode_ms_sum = 0.0;
    double stats_capture_ms_max = 0.0;
    double stats_infer_ms_max = 0.0;
    double stats_decode_ms_max = 0.0;

    bool high_precision_timer_set = false;
    if (aim::config::kUseHighPrecisionTimer)
    {
        high_precision_timer_set = (timeBeginPeriod(1) == TIMERR_NOERROR);
        if (aim::config::kEnableVerboseLog)
        {
            std::cout << "[初始化] 高精度计时器: " << (high_precision_timer_set ? "开启" : "失败") << "\n";
        }
    }

    auto next_capture_tick = Clock::now();
    bool pace_started = false;
    long long frame_index = 0;

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
                PreciseSleepUntil<Clock>(next_capture_tick);
            }
        }

        double capture_ms = 0.0;
        double infer_ms = 0.0;
        double decode_ms = 0.0;
        bool capture_ok = false;
        bool infer_ok = false;
        bool target_locked = false;
        bool has_target_for_log = false;
        aim::TargetPoint selected_target;

        cv::Mat frame;
        aim::Detections detections;

        const auto capture_start = Clock::now();
        capture_ok = capturer.CaptureFrame(frame);
        const auto capture_end = Clock::now();
        capture_ms = std::chrono::duration<double, std::milli>(capture_end - capture_start).count();

        if (capture_ok)
        {
            aim::RawTensor raw_output;
            const auto infer_start = Clock::now();
            infer_ok = infer.Run(frame, raw_output);
            const auto infer_end = Clock::now();
            infer_ms = std::chrono::duration<double, std::milli>(infer_end - infer_start).count();

            if (infer_ok)
            {
                const auto decode_start = Clock::now();
                decoder.Decode(raw_output, detections);
                const auto decode_end = Clock::now();
                decode_ms = std::chrono::duration<double, std::milli>(decode_end - decode_start).count();

                if (aim_enabled)
                {
                    const float capture_offset_x =
                        static_cast<float>(capturer.getWidth()) * 0.5f - static_cast<float>(aim::config::kCaptureSize) * 0.5f;
                    const float capture_offset_y =
                        static_cast<float>(capturer.getHeight()) * 0.5f - static_cast<float>(aim::config::kCaptureSize) * 0.5f;
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

                        const float dx_center = target.x - screen_center_x;
                        const float dy_center = target.y - screen_center_y;
                        const float active_radius = std::max(1.0f, aim::config::kAimActiveCircleRadiusPx);
                        const bool in_active_circle =
                            (dx_center * dx_center + dy_center * dy_center) <= (active_radius * active_radius);

                        if (in_active_circle)
                        {
                            aim_control.MoveToTarget(mouse, target.x, target.y, screen_width, screen_height);
                            target_locked = true;
                        }
                        else
                        {
                            // Outside center-lock circle: do not move to keep behavior more natural.
                            aim_control.Reset();
                        }
                    }
                    else
                    {
                        aim_control.Reset();
                    }
                }

                if (preview_enabled)
                {
                    DrawPreview(frame, detections, aim_enabled, target_head_class_id);
                    cv::imshow(kPreviewWindowName, frame);
                }
            }
        }

        ++stats_frames;
        stats_capture_ms_sum += capture_ms;
        stats_capture_ms_max = std::max(stats_capture_ms_max, capture_ms);
        if (capture_ok)
        {
            ++stats_capture_ok;
            ++stats_infer_attempts;
            stats_infer_ms_sum += infer_ms;
            stats_infer_ms_max = std::max(stats_infer_ms_max, infer_ms);
        }
        if (infer_ok)
        {
            ++stats_infer_ok;
            ++stats_decode_count;
            stats_decode_ms_sum += decode_ms;
            stats_decode_ms_max = std::max(stats_decode_ms_max, decode_ms);
            stats_last_detections = static_cast<int>(detections.size());
        }
        if (target_locked)
        {
            ++stats_target_locks;
        }

        if (KeyPressedEdge(aim::config::kHotkeyEnableAim, prev_q))
        {
            aim_enabled = true;
            target_selector.Reset();
            aim_control.Reset();
            std::cout << "[热键] 自瞄已开启\n";
        }
        if (KeyPressedEdge(aim::config::kHotkeyDisableAim, prev_k))
        {
            aim_enabled = false;
            target_selector.Reset();
            aim_control.Reset();
            std::cout << "[热键] 自瞄已关闭\n";
        }
        if (KeyPressedEdge(aim::config::kHotkeyTogglePreview, prev_v))
        {
            preview_enabled = !preview_enabled;
            if (!preview_enabled)
            {
                cv::destroyWindow(kPreviewWindowName);
            }
            std::cout << "[热键] 可视化已" << (preview_enabled ? "开启" : "关闭") << "\n";
        }
        if (KeyPressedEdge(aim::config::kHotkeyExit, prev_esc))
        {
            std::cout << "[热键] 退出\n";
            break;
        }

        if (tuning_csv.is_open())
        {
            const bool only_aim = aim::config::kTuningCsvLogOnlyAimEnabled;
            if (!only_aim || aim_enabled)
            {
                aim::AimDebugSnapshot dbg = {};
                if (target_locked)
                {
                    dbg = aim_control.LastDebug();
                }
                const double total_ms = capture_ms + infer_ms + decode_ms;
                const double t_ms =
                    std::chrono::duration<double, std::milli>(Clock::now() - app_start_time).count();

                tuning_csv << std::fixed << std::setprecision(3)
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

                ++tuning_csv_pending_flush;
                if (tuning_csv_pending_flush >= std::max(1, aim::config::kTuningCsvFlushIntervalFrames))
                {
                    tuning_csv.flush();
                    tuning_csv_pending_flush = 0;
                }
            }
        }

        if (aim::config::kEnableVerboseLog)
        {
            const auto now = Clock::now();
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - stats_window_start).count();
            if (elapsed_ms >= std::max(100, aim::config::kVerboseLogIntervalMs))
            {
                const double fps = stats_frames > 0 ? (1000.0 * static_cast<double>(stats_frames) / static_cast<double>(elapsed_ms)) : 0.0;
                const double cap_avg_ms = stats_frames > 0 ? (stats_capture_ms_sum / static_cast<double>(stats_frames)) : 0.0;
                const double infer_avg_ms = stats_infer_attempts > 0 ? (stats_infer_ms_sum / static_cast<double>(stats_infer_attempts)) : 0.0;
                const double decode_avg_ms = stats_decode_count > 0 ? (stats_decode_ms_sum / static_cast<double>(stats_decode_count)) : 0.0;
                const double total_avg_ms = cap_avg_ms + infer_avg_ms + decode_avg_ms;

                std::cout << std::fixed << std::setprecision(1)
                          << "[日志] FPS=" << fps
                          << " 截图(ms)=" << cap_avg_ms
                          << " 推理(ms)=" << infer_avg_ms
                          << " 总延迟(ms)=" << total_avg_ms
                          << "\n";

                stats_window_start = now;
                stats_frames = 0;
                stats_capture_ok = 0;
                stats_infer_ok = 0;
                stats_target_locks = 0;
                stats_last_detections = 0;
                stats_infer_attempts = 0;
                stats_decode_count = 0;
                stats_capture_ms_sum = 0.0;
                stats_infer_ms_sum = 0.0;
                stats_decode_ms_sum = 0.0;
                stats_capture_ms_max = 0.0;
                stats_infer_ms_max = 0.0;
                stats_decode_ms_max = 0.0;
            }
        }

        if (preview_enabled)
        {
            cv::waitKey(1);
        }

        if (!aim::config::kLimitCaptureRate && !preview_enabled)
        {
            Sleep(aim::config::kLoopSleepMs);
        }
    }

    if (high_precision_timer_set)
    {
        timeEndPeriod(1);
    }

    if (tuning_csv.is_open())
    {
        tuning_csv.flush();
        tuning_csv.close();
    }

    return 0;
}

