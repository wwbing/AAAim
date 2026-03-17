# Aim TRT Pack

这是一个实时目标检测与鼠标控制的 C++ 工程，当前主链路为：

`DXGI 截图 -> ONNX Runtime(TensorRT) 推理 -> YOLO 解码/NMS -> 目标选择 -> 鼠标移动`

本次重构后，项目只保留主程序 `aim_stable`，并新增了 ImGui 可交互控制面板用于实时调参。

## 目录结构

### 根目录
- `CMakeLists.txt`：构建入口（仅构建 `aim_stable`）。
- `README.md`：项目说明。

### `include/`
- `app_config.h`：默认参数、热键、推理配置常量。
- `pipeline_types.h`：跨模块数据结构（`RawTensor`、`Detection`、`TargetPoint`）。
- `screen_capture_dxgi.h`：DXGI 桌面复制截图。
- `ort_trt_infer.h`：ORT + TRT 推理接口。
- `yolo_decoder.h`：输出解码、NMS、阈值设置。
- `target_selector.h`：稳定目标选择器（锁定/丢失/门限逻辑）。
- `aim_control.h`：瞄准控制（相对/绝对移动、Direct/PID/Bezier 等算法）。
- `mouse_driver.h`：`ddll64.dll` 封装（`MoveTo` / `MoveR`）。
- `preview_renderer.h`：可视化叠加绘制。
- `perf_logger.h`：性能统计日志。
- `runtime_helpers.h`：模型路径、日志目录、热键边沿、精确 sleep 等工具。
- `runtime_tuning.h`：运行时可调参数结构。
- `control_panel.h`：ImGui 控制面板接口（Win32 + DX11）。

### `src/`
- `app_main.cpp`：主循环编排（采集/推理/控制/UI/热键/CSV）。
- `ort_trt_infer.cpp`：ORT Session 初始化与推理执行。
- `yolo_decoder.cpp`：YOLO 解码 + NMS。
- `target_selector.cpp`：候选目标筛选与稳定锁定逻辑。
- `aim_control.cpp`：鼠标移动控制核心实现。
- `preview_renderer.cpp`：检测框、状态文本、中心激活圈绘制。
- `perf_logger.cpp`：FPS/截图/推理/总延迟日志统计。
- `runtime_helpers.cpp`：运行时工具函数。
- `control_panel.cpp`：实时调参控制面板（ImGui 渲染与事件处理）。

### 资源
- `models/`：模型文件（默认优先 `CS2_1_CLS_Sim.onnx`）。
- `runtime/ddll64.dll`：鼠标驱动 DLL。
- `third_party/onnxruntime-win-x64-gpu-1.23.2/`：ORT GPU 预编译包。

## 当前热键

- `Q`：开启自瞄
- `K`：关闭自瞄
- `V`：切换检测可视化窗口
- `B`：切换控制面板窗口
- `F6`：退出程序

## 控制面板（实时生效）

控制面板支持以下参数实时调节：
- 置信度阈值
- NMS IoU
- 屏幕中心激活半径
- 瞄准平滑系数
- 最大单步移动
- 死区大小
- 可视化开关
- 性能日志开关

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
