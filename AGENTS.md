# Repository Guidelines

## Project Structure & Module Organization
This repository combines a Python YOLOv5 workflow and a Windows C++ helper project.

- `yolov5-6.0/`: main Python code for detection, training, runtime scripts, and configs.
- `yolov5-6.0/models`, `yolov5-6.0/utils`: model definitions and shared utilities.
- `yolov5-6.0/data`: dataset YAMLs and training metadata.
- `yolov5-6.0/runs`: generated training/inference outputs (artifacts).
- `dataset/`: local image/label data (`labels/`, `annotation/`).
- `11111/`: Visual Studio C++ solution (`11111.sln`, `yolov5.cpp`, headers, x64 build outputs).

Keep source changes in code/config folders; avoid committing generated binaries, caches, and run artifacts.

## Build, Test, and Development Commands
- `python -m venv .venv && .venv\Scripts\activate`: create and activate local env (Windows).
- `pip install -r yolov5-6.0/requirements.txt`: install base dependencies.
- `pip install -r yolov5-6.0/requirements_optimized.txt`: install optimized runtime dependencies (`dxcam`, etc.).
- `python yolov5-6.0/run.py`: start standard runtime.
- `python yolov5-6.0/run_optimized.py`: start optimized threaded runtime.
- `python yolov5-6.0/train.py --data yolov5-6.0/data/ball.yaml --weights yolov5s.pt`: run training.
- `python yolov5-6.0/val.py --weights yolov5-6.0/best.pt --data yolov5-6.0/data/ball.yaml`: validate model.
- `msbuild 11111\11111.sln /p:Configuration=Debug /p:Platform=x64`: build C++ project.

## Coding Style & Naming Conventions
Python follows PEP 8: 4-space indentation, `snake_case` functions/variables, `UPPER_CASE` constants, and `test_*.py` for test scripts. Keep config parameters centralized in `*_config.py` files. Use descriptive file names matching feature scope (for example, `game_sensitivity_config.py`).

C++ code in `11111/` should keep consistent brace style and descriptive method names matching existing `yolov5.cpp` patterns.

## Testing Guidelines
Current tests are script-based:
- `python yolov5-6.0/test_mouse_movement.py`
- `python yolov5-6.0/test_game_sensitivity.py`

For model changes, also run a short `val.py` pass and one runtime smoke test (`run.py` or `run_optimized.py`). Include basic result evidence (FPS, detection behavior, or error-free startup).

## Commit & Pull Request Guidelines
No `.git` metadata is present in this workspace snapshot, so historical commit conventions cannot be inferred here. Use clear, imperative commit messages, preferably Conventional Commit style (e.g., `feat: tune dxcam capture preset`).

PRs should include:
- What changed and why.
- Affected paths/configs.
- Test commands executed and outcomes.
- Screenshots/log snippets for UI/runtime behavior when relevant.
