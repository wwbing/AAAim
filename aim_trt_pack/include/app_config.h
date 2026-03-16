#pragma once

namespace aim {
namespace config {

// Capture
inline constexpr int kCaptureSize = 640;
inline constexpr bool kShowPreviewWindow = false;
inline constexpr int kLoopSleepMs = 0;
inline constexpr bool kLimitCaptureRate = false;
inline constexpr int kCaptureTargetFps = 30;
inline constexpr int kDxgiAcquireTimeoutMs = 1;
inline constexpr bool kUseHighPrecisionTimer = true;
inline constexpr bool kEnableVerboseLog = true;
inline constexpr int kVerboseLogIntervalMs = 1000;

// Detection
inline constexpr float kConfThreshold = 0.7f;
inline constexpr float kNmsIouThreshold = 0.6f;

// Aim control
inline constexpr float kAimSmoothFactor = 0.55f;
inline constexpr float kAimMaxStepPx = 300.0f;
inline constexpr float kAimDeadzonePx = 1.0f;
inline constexpr float kCursorLockCenterThresholdPx = 3.0f;

// Hotkeys
inline constexpr int kHotkeyEnableAim = 'Q';
inline constexpr int kHotkeyDisableAim = 'K';
inline constexpr int kHotkeyTogglePreview = 'V';
inline constexpr int kHotkeyExit = 0x75; // VK_F6

// TensorRT provider options
inline constexpr bool kTrtFp16Enable = true;
inline constexpr bool kTrtEngineCacheEnable = true;
inline constexpr bool kTrtTimingCacheEnable = false;
inline constexpr bool kTrtForceSequentialEngineBuild = false;
inline constexpr int kTrtBuilderOptimizationLevel = 5;

} // namespace config
} // namespace aim
