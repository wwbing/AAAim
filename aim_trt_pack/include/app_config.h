#pragma once

namespace aim {
namespace config {

// Capture
inline constexpr int kCaptureSize = 640;
inline constexpr bool kShowPreviewWindow = false;
inline constexpr int kLoopSleepMs = 1;

// Detection
inline constexpr float kConfThreshold = 0.25f;
inline constexpr float kNmsIouThreshold = 0.6f;

// Aim control
inline constexpr float kAimSmoothFactor = 0.28f;
inline constexpr float kAimMaxStepPx = 140.0f;
inline constexpr float kAimDeadzonePx = 2.0f;
inline constexpr float kCursorLockCenterThresholdPx = 3.0f;

// Hotkeys
inline constexpr int kHotkeyEnableAim = 'Q';
inline constexpr int kHotkeyDisableAim = 'K';
inline constexpr int kHotkeyExit = 27; // VK_ESCAPE

// TensorRT provider options
inline constexpr bool kTrtFp16Enable = true;
inline constexpr bool kTrtEngineCacheEnable = true;
inline constexpr bool kTrtTimingCacheEnable = false;
inline constexpr bool kTrtForceSequentialEngineBuild = false;
inline constexpr int kTrtBuilderOptimizationLevel = 2;

} // namespace config
} // namespace aim

