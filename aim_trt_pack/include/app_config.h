#pragma once

namespace aim {
namespace config {

enum class AimAlgorithm {
    Proportional = 0,
    PID = 1,
    Bezier = 2,
};

enum class AimMoveMode {
    Auto = 0,
    Relative = 1,
    Absolute = 2,
};

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
inline constexpr bool kEnableTuningCsvLog = true;
inline constexpr bool kTuningCsvLogOnlyAimEnabled = true;
inline constexpr int kTuningCsvFlushIntervalFrames = 60;

// Detection
inline constexpr float kConfThreshold = 0.75f;
inline constexpr float kNmsIouThreshold = 0.45f;
inline constexpr float kMinTargetBoxWidthPx = 10.0f;
inline constexpr float kMinTargetBoxHeightPx = 10.0f;
inline constexpr float kMaxTargetBoxWidthPx = 320.0f;
inline constexpr float kMaxTargetBoxHeightPx = 320.0f;
inline constexpr float kTargetMinAspectRatio = 0.5f;  // h/w
inline constexpr float kTargetMaxAspectRatio = 2.4f;  // h/w
inline constexpr float kTargetAcquireGatePx = 72.0f;
inline constexpr float kTargetTrackGatePx = 180.0f;
inline constexpr int kTargetAcquireConfirmFrames = 1;
inline constexpr int kTargetLostToleranceFrames = 12;
inline constexpr float kTargetTrackSmoothing = 0.92f; // Larger is more stable, less responsive.

// Aim control
inline constexpr AimAlgorithm kAimAlgorithm = AimAlgorithm::PID;
inline constexpr AimMoveMode kAimMoveMode = AimMoveMode::Relative;
inline constexpr float kAimSmoothFactor = 0.42f;
inline constexpr float kAimMaxStepPx = 180.0f;
inline constexpr float kAimDeadzonePx = 2.2f;
inline constexpr float kCursorLockCenterThresholdPx = 3.0f;
inline constexpr float kAimPidKi = 0.0010f;
inline constexpr float kAimPidKd = 0.014f;
inline constexpr float kAimPidDerivativeAlpha = 0.92f;
inline constexpr float kAimPidIntegralLimit = 180.0f;
inline constexpr float kAimBezierStrength = 0.16f;
inline constexpr float kAimBezierDistancePx = 220.0f;
inline constexpr float kAimNearZonePx = 18.0f;
inline constexpr float kAimNearZoneMinGain = 0.12f;
inline constexpr float kAimOutputLpfAlpha = 0.50f;
inline constexpr float kAimRelativeMinStepErrorPx = 3.5f;
inline constexpr float kAimNearZoneLpfAlpha = 0.30f;
inline constexpr float kAimSignFlipDamping = 0.08f;
inline constexpr float kAimOutputOvershootRatio = 0.90f;
inline constexpr float kAimNearZoneMaxCmdRatio = 0.62f;
inline constexpr float kAimMicroErrorPx = 3.2f;
inline constexpr float kAimMicroMoveDeadbandPx = 0.45f;
inline constexpr float kAimFlipSuppressErrorPx = 6.5f;
inline constexpr int kAimFlipSuppressCmdPx = 2;
inline constexpr float kAimQuantizerCarryDecay = 0.75f;
inline constexpr float kAimQuantizerFlipDamping = 0.12f;
inline constexpr float kAimQuantizerMaxCarryPx = 4.0f;
inline constexpr float kAimQuantizerEnableErrorPx = 8.0f;
inline constexpr float kAimStickyHoldEnterPx = 2.2f;
inline constexpr float kAimStickyHoldExitPx = 4.8f;

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
