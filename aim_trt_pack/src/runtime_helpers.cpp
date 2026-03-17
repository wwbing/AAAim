#define NOMINMAX
#include <windows.h>

#include "runtime_helpers.h"

#include <clocale>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string DirnameOf(const std::string& path)
{
    const size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
    {
        return ".";
    }
    return path.substr(0, pos);
}

} // namespace

namespace aim {

bool FileExists(const std::string& path)
{
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
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
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool KeyPressedEdge(int vk, bool& prev_down)
{
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool edge = down && !prev_down;
    prev_down = down;
    return edge;
}

void ConfigureConsoleUtf8()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
}

void EnableDpiAwareness()
{
    SetProcessDPIAware();
}

void PreciseSleepUntil(const std::chrono::steady_clock::time_point& target_time)
{
    while (true)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= target_time)
        {
            return;
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(target_time - now);
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

} // namespace aim

