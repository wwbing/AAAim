# Aim TRT Pack

实时检测与鼠标控制主链路：

`DXGI 截图 -> ONNX Runtime(TensorRT) 推理 -> YOLO 解码/NMS -> 目标选择 -> 鼠标移动`

当前版本已经完成精简与解耦，只保留主程序 `aim_stable`，并使用 **ImGui (Win32 + DX11)** 作为唯一控制面板。

## 主要特性

- 所有功能开关都在 ImGui 面板中控制，不再依赖热键。
- 参数支持实时生效（检测阈值、NMS、瞄准参数等）。
- 已关闭 CSV 调参日志，不再生成 CSV 文件。

## 目录说明

- `include/app_config.h`：默认常量配置。
- `include/runtime_tuning.h`：运行时可调参数结构。
- `include/control_panel.h`：ImGui 控制面板接口。
- `include/target_selector.h`：目标锁定策略。
- `include/aim_control.h`：鼠标控制策略。
- `include/runtime_helpers.h`：运行时通用工具。
- `src/app_main.cpp`：主循环（采集、推理、控制、UI）。
- `src/control_panel.cpp`：ImGui 窗口、渲染与交互。
- `src/ort_trt_infer.cpp`：ORT + TensorRT 推理模块。
- `src/yolo_decoder.cpp`：解码与 NMS。
- `src/target_selector.cpp`：目标筛选与稳定跟踪。
- `src/perf_logger.cpp`：性能统计输出。
- `src/preview_renderer.cpp`：可视化绘制。

## 构建

在 `aim_trt_pack` 目录执行（PowerShell）：

```powershell
$cmd='"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake -S . -B build-nmake -G "NMake Makefiles" -DOpenCV_DIR="D:/opencv/installed/x64-windows/share/opencv4" -DCMAKE_PREFIX_PATH="D:/opencv/installed/x64-windows;D:/vcpkg/installed/x64-windows" && cmake --build build-nmake --target aim_stable'
cmd /c $cmd
```

## 运行

```powershell
.\build-nmake\aim_stable.exe
```

