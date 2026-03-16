# Aim TRT Pack

当前版本是精简后的模块化实现：`DXGI 截图 -> ORT(TensorRT) 推理 -> YOLO 解码/选目标 -> 鼠标控制`。  
已删除旧实验文件，主程序只保留自瞄开关必要日志。

## 目录与文件职责
### 根目录
- `CMakeLists.txt`：构建配置，只生成 `aim_stable`。
- `README.md`：本说明文档。

### include/
- `app_config.h`：统一参数配置中心。后续调参只改这个文件。
- `screen_capture_dxgi.h`：屏幕中心区域截图（DXGI Desktop Duplication）。
- `mouse_driver.h`：鼠标设备 DLL 封装（加载 `ddll64.dll`，提供 `MoveTo/MoveR`）。
- `pipeline_types.h`：跨模块共享数据结构（RawTensor、Detection、TargetPoint）。
- `ort_trt_infer.h`：推理模块接口（初始化 TRT、执行推理）。
- `yolo_decoder.h`：解码模块接口（阈值过滤、NMS、最近目标选择）。
- `aim_control.h`：控制模块接口（平滑移动、相对/绝对模式切换）。

### src/
- `app_main.cpp`：主流程编排与热键逻辑（Q 开、K 关、ESC 退出）。
- `ort_trt_infer.cpp`：ORT+TRT Session 初始化和单帧推理。
- `yolo_decoder.cpp`：模型输出解码、NMS、最近目标选择。
- `aim_control.cpp`：根据目标坐标执行鼠标移动。

### 资源
- `models/best-sim.onnx`：模型文件。
- `runtime/ddll64.dll`：鼠标驱动 DLL。
- `third_party/onnxruntime-win-x64-gpu-1.23.2/`：ONNX Runtime GPU 预编译包。

## 已删除内容
- `src/bench_capture.cpp`
- `src/aim_autolock_ort_trt.cpp`

## 参数集中管理（重点）
所有常用参数已集中到 `include/app_config.h`：
- 截图：`kCaptureSize`、`kShowPreviewWindow`、`kLoopSleepMs`
- 检测：`kConfThreshold`、`kNmsIouThreshold`
- 控制：`kAimSmoothFactor`、`kAimMaxStepPx`、`kAimDeadzonePx`、`kCursorLockCenterThresholdPx`
- 热键：`kHotkeyEnableAim`、`kHotkeyDisableAim`、`kHotkeyExit`
- TRT：`kTrtFp16Enable`、`kTrtEngineCacheEnable`、`kTrtTimingCacheEnable`、`kTrtForceSequentialEngineBuild`、`kTrtBuilderOptimizationLevel`

后续调参只改 `app_config.h`，不需要在多个 `.cpp` 里重复改。

## 构建
在 `aim_trt_pack` 目录执行（PowerShell）：

```powershell
$cmd='"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake -S . -B build-nmake -G "NMake Makefiles" -DOpenCV_DIR="D:/opencv/installed/x64-windows/share/opencv4" -DCMAKE_PREFIX_PATH="D:/opencv/installed/x64-windows" && cmake --build build-nmake --target aim_stable'
cmd /c $cmd
```

## 运行
```powershell
.\build-nmake\aim_stable.exe
```

## 当前行为
- 仅在热键状态变化时打印日志。
- 目标策略固定为“最近屏幕中心目标”。
- 鼠标优先走相对移动（游戏锁鼠标场景更稳定）。
