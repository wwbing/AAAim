#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <conio.h>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

using OpenDeviceFn = int (*)();
using MoveToFn = void (*)(unsigned short, unsigned short);
using MoveRFn = void (*)(int, int);
using MoveDFn = void (*)(unsigned short, unsigned short, int);
using MoveRDFn = void (*)(int, int, int);

struct Endpoints {
    int start_x = 0;
    int start_y = 0;
    int target_x = 0;
    int target_y = 0;
};

std::string DirnameOf(const std::string& path)
{
    const size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

bool FileExists(const std::string& path)
{
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string ResolveDllPath()
{
    std::vector<std::string> candidates = {
        "ddll64.dll",
        "runtime\\ddll64.dll"
    };

    char exe_path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
        const std::string exe_dir = DirnameOf(std::string(exe_path));
        candidates.push_back(exe_dir + "\\ddll64.dll");
        candidates.push_back(exe_dir + "\\..\\runtime\\ddll64.dll");
        candidates.push_back(exe_dir + "\\..\\..\\runtime\\ddll64.dll");
    }

    for (const auto& p : candidates) {
        if (FileExists(p)) {
            return p;
        }
    }
    return "ddll64.dll";
}

bool WaitCursorStable(int timeout_ms, int stable_need_ms)
{
    POINT prev = {};
    GetCursorPos(&prev);
    auto stable_start = Clock::now();
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

    while (Clock::now() < deadline) {
        POINT p = {};
        GetCursorPos(&p);
        const int md = std::abs(p.x - prev.x) + std::abs(p.y - prev.y);
        if (md <= 1) {
            const auto stable_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - stable_start).count();
            if (stable_ms >= stable_need_ms) {
                return true;
            }
        } else {
            stable_start = Clock::now();
            prev = p;
        }
        Sleep(1);
    }
    return false;
}

bool SafeCallMoveTo(MoveToFn fn, unsigned short x, unsigned short y)
{
    if (fn == nullptr) {
        return false;
    }
#ifdef _MSC_VER
    __try {
        fn(x, y);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    fn(x, y);
    return true;
#endif
}

bool SafeCallMoveR(MoveRFn fn, int dx, int dy)
{
    if (fn == nullptr) {
        return false;
    }
#ifdef _MSC_VER
    __try {
        fn(dx, dy);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    fn(dx, dy);
    return true;
#endif
}

bool SafeCallMoveD(MoveDFn fn, unsigned short x, unsigned short y, int duration_ms)
{
    if (fn == nullptr) {
        return false;
    }
#ifdef _MSC_VER
    __try {
        fn(x, y, duration_ms);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    fn(x, y, duration_ms);
    return true;
#endif
}

bool SafeCallMoveRD(MoveRDFn fn, int dx, int dy, int duration_ms)
{
    if (fn == nullptr) {
        return false;
    }
#ifdef _MSC_VER
    __try {
        fn(dx, dy, duration_ms);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    fn(dx, dy, duration_ms);
    return true;
#endif
}

unsigned short ToNorm16(int pixel_value, int pixel_max)
{
    if (pixel_max <= 1) {
        return 0;
    }
    const double n = static_cast<double>(pixel_value) * 65535.0 / static_cast<double>(pixel_max - 1);
    const double c = std::clamp(n, 0.0, 65535.0);
    return static_cast<unsigned short>(std::lround(c));
}

Endpoints BuildEndpoints(int screen_w, int screen_h, bool right_upper_target)
{
    Endpoints ep;
    const int margin_x = std::max(40, screen_w / 25); // 2560 -> 102
    const int margin_y = std::max(40, screen_h / 14); // 1660 -> 118

    ep.start_x = margin_x;
    ep.start_y = screen_h - margin_y;

    ep.target_x = screen_w - margin_x;
    ep.target_y = right_upper_target ? margin_y : (screen_h - margin_y);
    return ep;
}

bool IsNearPoint(const POINT& p, int x, int y, int tol_px)
{
    return std::abs(p.x - x) <= tol_px && std::abs(p.y - y) <= tol_px;
}

bool MoveToStartAndVerify(const Endpoints& ep, int tol_px, int settle_ms)
{
    SetCursorPos(ep.start_x, ep.start_y);
    Sleep(settle_ms);

    POINT p = {};
    GetCursorPos(&p);
    if (IsNearPoint(p, ep.start_x, ep.start_y, tol_px)) {
        return true;
    }

    // Retry once
    SetCursorPos(ep.start_x, ep.start_y);
    Sleep(settle_ms);
    GetCursorPos(&p);
    return IsNearPoint(p, ep.start_x, ep.start_y, tol_px);
}

void PrintMenu()
{
    std::cout
        << "\n=== DLL 轨迹交互测试 ===\n"
        << "1: MoveTo（绝对坐标）\n"
        << "2: MoveR （相对位移）\n"
        << "3: MoveD （绝对像素 + arg3）\n"
        << "4: MoveRD（相对像素 + arg3）\n"
        << "5: MoveD （绝对归一化 0..65535 + arg3）\n"
        << "S: 仅移动到起点并校验\n"
        << "P: 打印当前状态\n"
        << "B: 目标=右下角\n"
        << "U: 目标=右上角\n"
        << "I: 切换 MoveRD 的 Y 反向\n"
        << "[-]/[+]: arg3 -/+ 20\n"
        << "[,]/[.]: arg3 -/+ 1\n"
        << "Q 或 ESC: 退出\n"
        << "请选择: ";
}

void PrintState(
    const Endpoints& ep,
    bool target_is_upper,
    int arg3,
    int tol_px,
    bool rd_invert_y,
    int screen_w,
    int screen_h)
{
    POINT p = {};
    GetCursorPos(&p);
    const unsigned short n_tx = ToNorm16(ep.target_x, screen_w);
    const unsigned short n_ty = ToNorm16(ep.target_y, screen_h);
    std::cout
        << "\n[状态] 当前=(" << p.x << "," << p.y << ")"
        << " 起点=(" << ep.start_x << "," << ep.start_y << ")"
        << " 终点=(" << ep.target_x << "," << ep.target_y << ")"
        << " 终点归一化16=(" << n_tx << "," << n_ty << ")"
        << " 目标模式=" << (target_is_upper ? "右上角" : "右下角")
        << " arg3=" << arg3
        << " RD反转Y=" << (rd_invert_y ? "开" : "关")
        << " 起点容差像素=" << tol_px
        << "\n";
}

} // namespace

int main()
{
    SetProcessDPIAware();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    const std::string dll_path = ResolveDllPath();
    std::cout << "DLL 路径: " << dll_path << "\n";

    HMODULE h = LoadLibraryA(dll_path.c_str());
    if (h == nullptr) {
        std::cerr << "加载 DLL 失败。\n";
        return 1;
    }

    auto open_device = reinterpret_cast<OpenDeviceFn>(GetProcAddress(h, "OpenDevice"));
    auto move_to = reinterpret_cast<MoveToFn>(GetProcAddress(h, "MoveTo"));
    auto move_r = reinterpret_cast<MoveRFn>(GetProcAddress(h, "MoveR"));
    auto move_d = reinterpret_cast<MoveDFn>(GetProcAddress(h, "MoveD"));
    auto move_rd = reinterpret_cast<MoveRDFn>(GetProcAddress(h, "MoveRD"));

    std::cout << "[导出] MoveTo: " << (move_to ? "有" : "无") << "\n";
    std::cout << "[导出] MoveR : " << (move_r ? "有" : "无") << "\n";
    std::cout << "[导出] MoveD : " << (move_d ? "有" : "无")
              << "（假设签名: (x,y,arg3)）\n";
    std::cout << "[导出] MoveRD: " << (move_rd ? "有" : "无")
              << "（假设签名: (dx,dy,arg3)）\n";

    if (open_device == nullptr || open_device() == 0) {
        std::cerr << "OpenDevice 失败，设备未就绪。\n";
        FreeLibrary(h);
        return 1;
    }

    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);
    bool target_is_upper = true;
    int arg3 = 180;
    const int timeout_ms = 2500;
    const int settle_ms = 180;
    const int start_tol_px = 6;
    bool rd_invert_y = false;

    Endpoints ep = BuildEndpoints(screen_w, screen_h, target_is_upper);

    std::cout << "屏幕分辨率: " << screen_w << "x" << screen_h << "\n";
    PrintState(ep, target_is_upper, arg3, start_tol_px, rd_invert_y, screen_w, screen_h);

    while (true) {
        PrintMenu();
        const int key = _getch();
        if (key == 27 || key == 'q' || key == 'Q') {
            std::cout << "\n退出。\n";
            break;
        }

        if (key == '+' || key == '=') {
            arg3 = std::min(5000, arg3 + 20);
            std::cout << "\n[设置] arg3=" << arg3 << "\n";
            continue;
        }
        if (key == '-' || key == '_') {
            arg3 = std::max(0, arg3 - 20);
            std::cout << "\n[设置] arg3=" << arg3 << "\n";
            continue;
        }
        if (key == '.' || key == '>') {
            arg3 = std::min(5000, arg3 + 1);
            std::cout << "\n[设置] arg3=" << arg3 << "\n";
            continue;
        }
        if (key == ',' || key == '<') {
            arg3 = std::max(0, arg3 - 1);
            std::cout << "\n[设置] arg3=" << arg3 << "\n";
            continue;
        }
        if (key == 'b' || key == 'B') {
            target_is_upper = false;
            ep = BuildEndpoints(screen_w, screen_h, target_is_upper);
            std::cout << "\n[设置] 目标模式=右下角\n";
            PrintState(ep, target_is_upper, arg3, start_tol_px, rd_invert_y, screen_w, screen_h);
            continue;
        }
        if (key == 'u' || key == 'U') {
            target_is_upper = true;
            ep = BuildEndpoints(screen_w, screen_h, target_is_upper);
            std::cout << "\n[设置] 目标模式=右上角\n";
            PrintState(ep, target_is_upper, arg3, start_tol_px, rd_invert_y, screen_w, screen_h);
            continue;
        }
        if (key == 'i' || key == 'I') {
            rd_invert_y = !rd_invert_y;
            std::cout << "\n[设置] MoveRD 反转Y=" << (rd_invert_y ? "开" : "关") << "\n";
            continue;
        }
        if (key == 'p' || key == 'P') {
            PrintState(ep, target_is_upper, arg3, start_tol_px, rd_invert_y, screen_w, screen_h);
            continue;
        }
        if (key == 's' || key == 'S') {
            const bool ok = MoveToStartAndVerify(ep, start_tol_px, settle_ms);
            POINT p = {};
            GetCursorPos(&p);
            std::cout << "\n[起点校验] " << (ok ? "成功" : "失败")
                      << " 当前=(" << p.x << "," << p.y << ")\n";
            continue;
        }

        if (key != '1' && key != '2' && key != '3' && key != '4' && key != '5') {
            std::cout << "\n[警告] 无效按键。\n";
            continue;
        }

        const bool start_ok = MoveToStartAndVerify(ep, start_tol_px, settle_ms);
        POINT before = {};
        GetCursorPos(&before);
        if (!start_ok) {
            std::cout << "\n[错误] 起点校验失败。"
                      << " 期望=(" << ep.start_x << "," << ep.start_y << ")"
                      << " 实际=(" << before.x << "," << before.y << ")\n";
            continue;
        }

        const int dx = ep.target_x - ep.start_x;
        const int dy = ep.target_y - ep.start_y;
        const int dy_rd = rd_invert_y ? -dy : dy;
        bool invoke_ok = false;
        const auto t0 = Clock::now();
        std::string mode_name;
        std::string call_info;

        if (key == '1') {
            mode_name = "MoveTo";
            invoke_ok = SafeCallMoveTo(
                move_to,
                static_cast<unsigned short>(ep.target_x),
                static_cast<unsigned short>(ep.target_y));
            call_info = "MoveTo(x=" + std::to_string(ep.target_x) + ",y=" + std::to_string(ep.target_y) + ")";
        }
        else if (key == '2') {
            mode_name = "MoveR";
            invoke_ok = SafeCallMoveR(move_r, dx, dy);
            call_info = "MoveR(dx=" + std::to_string(dx) + ",dy=" + std::to_string(dy) + ")";
        }
        else if (key == '3') {
            mode_name = "MoveD";
            invoke_ok = SafeCallMoveD(
                move_d,
                static_cast<unsigned short>(ep.target_x),
                static_cast<unsigned short>(ep.target_y),
                arg3);
            call_info = "MoveD(x=" + std::to_string(ep.target_x) + ",y=" + std::to_string(ep.target_y)
                + ",arg3=" + std::to_string(arg3) + ")";
        }
        else if (key == '4') {
            mode_name = "MoveRD";
            invoke_ok = SafeCallMoveRD(move_rd, dx, dy_rd, arg3);
            call_info = "MoveRD(dx=" + std::to_string(dx) + ",dy=" + std::to_string(dy_rd)
                + ",arg3=" + std::to_string(arg3) + ")";
        }
        else if (key == '5') {
            mode_name = "MoveDNorm16";
            const unsigned short nx = ToNorm16(ep.target_x, screen_w);
            const unsigned short ny = ToNorm16(ep.target_y, screen_h);
            invoke_ok = SafeCallMoveD(move_d, nx, ny, arg3);
            call_info = "MoveD(x16=" + std::to_string(nx) + ",y16=" + std::to_string(ny)
                + ",arg3=" + std::to_string(arg3) + ")";
        }

        const bool stable = WaitCursorStable(timeout_ms, 140);
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        POINT after = {};
        GetCursorPos(&after);

        std::cout << "\n[运行] 模式=" << mode_name
                  << " 调用=" << call_info
                  << " 调用成功=" << (invoke_ok ? "是" : "否")
                  << " 已稳定=" << (stable ? "是" : "否")
                  << " 耗时ms=" << elapsed_ms
                  << " 起始位置=(" << before.x << "," << before.y << ")"
                  << " 结束位置=(" << after.x << "," << after.y << ")"
                  << " 目标位置=(" << ep.target_x << "," << ep.target_y << ")"
                  << " 与目标差值=(" << (ep.target_x - after.x) << "," << (ep.target_y - after.y) << ")"
                  << " arg3=" << arg3
                  << "\n";
    }

    FreeLibrary(h);
    return 0;
}
