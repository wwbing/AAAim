#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <iostream>
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

namespace {

const std::vector<std::string> kClassNames = { "ball" };

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

std::string ResolveModelPath()
{
    std::vector<std::string> candidates = {
        "models\\best-sim.onnx",
        "best-sim.onnx"
    };

    char exe_path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0)
    {
        const std::string exe_dir = DirnameOf(std::string(exe_path));
        candidates.push_back(exe_dir + "\\best-sim.onnx");
        candidates.push_back(exe_dir + "\\models\\best-sim.onnx");
        candidates.push_back(exe_dir + "\\..\\..\\models\\best-sim.onnx");
    }

    for (const auto& p : candidates)
    {
        if (FileExists(p))
        {
            return p;
        }
    }

    return "models\\best-sim.onnx";
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

void DrawPreview(cv::Mat& frame, const aim::Detections& detections)
{
    for (const auto& det : detections)
    {
        const cv::Point p1(static_cast<int>(det[0]), static_cast<int>(det[1]));
        const cv::Point p2(static_cast<int>(det[2]), static_cast<int>(det[3]));
        cv::rectangle(frame, p1, p2, cv::Scalar(0, 255, 0), 2);
        const int cls_idx = static_cast<int>(det[5]);
        const std::string cls_name =
            (cls_idx >= 0 && cls_idx < static_cast<int>(kClassNames.size())) ? kClassNames[cls_idx] : "obj";
        cv::putText(
            frame,
            cls_name + " " + std::to_string(det[4]),
            p1,
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cv::Scalar(0, 255, 0),
            1);
    }
}

} // namespace

int main()
{
    EnableDpiAwareness();

    ScreenCapturer capturer(aim::config::kCaptureSize, aim::config::kCaptureSize);
    const int screen_width = capturer.getWidth();
    const int screen_height = capturer.getHeight();
    std::cout << "Screen: " << screen_width << "x" << screen_height << "\n";
    std::cout << "Capture backend: DXGI Desktop Duplication\n";
    std::cout << "Hotkeys(global): [Q]=Enable aim, [K]=Disable aim, [ESC]=Exit\n";

    const std::string model_path = ResolveModelPath();
    std::cout << "Model path: " << model_path << "\n";

    aim::OrtTrtInfer infer(aim::config::kCaptureSize);
    if (!infer.Initialize(model_path))
    {
        std::cerr << "Failed to initialize TRT inference backend.\n";
        return 1;
    }
    std::cout << "Inference backend: ONNX Runtime (" << infer.BackendName() << ")\n";

    MouseController mouse;
    if (!mouse.Initialize())
    {
        return 1;
    }

    aim::YoloDecoder decoder(aim::config::kConfThreshold, aim::config::kNmsIouThreshold);
    aim::AimControl aim_control(
        aim::config::kAimSmoothFactor,
        aim::config::kAimMaxStepPx,
        aim::config::kAimDeadzonePx,
        aim::config::kCursorLockCenterThresholdPx);

    bool aim_enabled = false;
    bool prev_q = false;
    bool prev_k = false;
    bool prev_esc = false;

    while (true)
    {
        cv::Mat frame;
        if (capturer.CaptureFrame(frame))
        {
            aim::RawTensor raw_output;
            if (infer.Run(frame, raw_output))
            {
                aim::Detections detections;
                decoder.Decode(raw_output, detections);

                if (aim_enabled)
                {
                    const float capture_offset_x =
                        static_cast<float>(capturer.getWidth()) * 0.5f - static_cast<float>(aim::config::kCaptureSize) * 0.5f;
                    const float capture_offset_y =
                        static_cast<float>(capturer.getHeight()) * 0.5f - static_cast<float>(aim::config::kCaptureSize) * 0.5f;
                    const float screen_center_x = static_cast<float>(screen_width) * 0.5f;
                    const float screen_center_y = static_cast<float>(screen_height) * 0.5f;

                    aim::TargetPoint target;
                    if (decoder.SelectNearestTarget(
                            detections,
                            capture_offset_x,
                            capture_offset_y,
                            screen_center_x,
                            screen_center_y,
                            target))
                    {
                        target.x = std::clamp(target.x, 0.0f, static_cast<float>(screen_width - 1));
                        target.y = std::clamp(target.y, 0.0f, static_cast<float>(screen_height - 1));
                        aim_control.MoveToTarget(mouse, target.x, target.y, screen_width, screen_height);
                    }
                }

                if (aim::config::kShowPreviewWindow)
                {
                    DrawPreview(frame, detections);
                    cv::imshow("Detection", frame);
                }
            }
        }

        if (KeyPressedEdge(aim::config::kHotkeyEnableAim, prev_q))
        {
            aim_enabled = true;
            std::cout << "[HOTKEY] Aim ENABLED\n";
        }
        if (KeyPressedEdge(aim::config::kHotkeyDisableAim, prev_k))
        {
            aim_enabled = false;
            std::cout << "[HOTKEY] Aim DISABLED\n";
        }
        if (KeyPressedEdge(aim::config::kHotkeyExit, prev_esc))
        {
            std::cout << "[HOTKEY] Exit\n";
            break;
        }

        if (aim::config::kShowPreviewWindow)
        {
            cv::waitKey(1);
        }
        else
        {
            Sleep(aim::config::kLoopSleepMs);
        }
    }

    return 0;
}

