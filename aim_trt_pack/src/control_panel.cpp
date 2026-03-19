#include "control_panel.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);

namespace {

constexpr const char* kWindowClassName = "AimControlPanelWindowClass";
constexpr const char* kWindowTitle = "自瞄控制面板";

void SafeRelease(IUnknown* ptr)
{
    if (ptr != nullptr)
    {
        ptr->Release();
    }
}

void LoadChineseFont()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* glyph_ranges = io.Fonts->GetGlyphRangesChineseFull();
    const std::array<const char*, 5> font_paths = {
        "C:\\Windows\\Fonts\\msyh.ttc",   // 寰蒋闆呴粦
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf", // 榛戜綋
        "C:\\Windows\\Fonts\\simsun.ttc", // 瀹嬩綋
        "C:\\Windows\\Fonts\\Deng.ttf"    // 绛夌嚎
    };

    for (const char* path : font_paths)
    {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }

        ImFont* font = io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, glyph_ranges);
        if (font != nullptr)
        {
            io.FontDefault = font;
            return;
        }
    }
}

} // namespace

namespace aim {

bool ControlPanel::Initialize(const RuntimeTuning& tuning)
{
    if (initialized_)
    {
        SyncFromRuntime(tuning);
        return true;
    }

    tuning_cache_ = tuning;
    ApplyRuntimeLimits(tuning_cache_);

    if (!CreateWindowAndDevice())
    {
        Destroy();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    LoadChineseFont();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(d3d_device_, d3d_device_context_);

    initialized_ = true;
    SetVisible(true);
    return true;
}

void ControlPanel::Destroy()
{
    if (!initialized_)
    {
        return;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupD3DDevice();
    if (hwnd_ != nullptr)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (window_class_.lpszClassName != nullptr && window_class_.hInstance != nullptr)
    {
        UnregisterClassA(window_class_.lpszClassName, window_class_.hInstance);
        window_class_ = {};
    }

    initialized_ = false;
    visible_ = false;
}

void ControlPanel::SetVisible(bool visible)
{
    if (!initialized_ || hwnd_ == nullptr)
    {
        return;
    }

    visible_ = visible;
    ShowWindow(hwnd_, visible_ ? SW_SHOW : SW_HIDE);
}

void ControlPanel::SyncFromRuntime(const RuntimeTuning& tuning)
{
    tuning_cache_ = tuning;
    ApplyRuntimeLimits(tuning_cache_);
}

void ControlPanel::Poll(RuntimeTuning& tuning)
{
    if (!initialized_)
    {
        return;
    }

    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (!visible_)
    {
        return;
    }

    RenderUi(tuning);
}

bool ControlPanel::CreateWindowAndDevice()
{
    window_class_.cbSize = sizeof(WNDCLASSEXA);
    window_class_.style = CS_CLASSDC;
    window_class_.lpfnWndProc = WndProc;
    window_class_.cbClsExtra = 0;
    window_class_.cbWndExtra = 0;
    window_class_.hInstance = GetModuleHandleA(nullptr);
    window_class_.hIcon = nullptr;
    window_class_.hCursor = nullptr;
    window_class_.hbrBackground = nullptr;
    window_class_.lpszMenuName = nullptr;
    window_class_.lpszClassName = kWindowClassName;
    window_class_.hIconSm = nullptr;

    if (RegisterClassExA(&window_class_) == 0)
    {
        return false;
    }

    const DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    hwnd_ = CreateWindowA(
        kWindowClassName,
        kWindowTitle,
        window_style,
        180,
        180,
        560,
        520,
        nullptr,
        nullptr,
        window_class_.hInstance,
        this);
    if (hwnd_ == nullptr)
    {
        return false;
    }

    if (!CreateD3DDevice())
    {
        return false;
    }

    ShowWindow(hwnd_, SW_HIDE);
    UpdateWindow(hwnd_);
    visible_ = false;
    return true;
}

bool ControlPanel::CreateD3DDevice()
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT create_device_flags = 0;
    const D3D_FEATURE_LEVEL feature_level_array[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        create_device_flags,
        feature_level_array,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &swap_chain_,
        &d3d_device_,
        &feature_level,
        &d3d_device_context_);
    if (FAILED(hr))
    {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void ControlPanel::CleanupD3DDevice()
{
    CleanupRenderTarget();
    if (swap_chain_ != nullptr)
    {
        swap_chain_->Release();
        swap_chain_ = nullptr;
    }
    if (d3d_device_context_ != nullptr)
    {
        d3d_device_context_->Release();
        d3d_device_context_ = nullptr;
    }
    if (d3d_device_ != nullptr)
    {
        d3d_device_->Release();
        d3d_device_ = nullptr;
    }
}

void ControlPanel::CreateRenderTarget()
{
    if (swap_chain_ == nullptr)
    {
        return;
    }

    ID3D11Texture2D* back_buffer = nullptr;
    if (SUCCEEDED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
    {
        d3d_device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_view_);
    }
    SafeRelease(back_buffer);
}

void ControlPanel::CleanupRenderTarget()
{
    if (render_target_view_ != nullptr)
    {
        render_target_view_->Release();
        render_target_view_ = nullptr;
    }
}

void ControlPanel::HandleResize(unsigned int width, unsigned int height)
{
    if (swap_chain_ == nullptr || width == 0 || height == 0)
    {
        return;
    }

    CleanupRenderTarget();
    swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void ControlPanel::RenderUi(RuntimeTuning& tuning)
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(
        "主面板",
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("实时参数控制（修改后立即生效）");
    ImGui::Separator();

    if (tuning_cache_.init_in_progress || tuning_cache_.init_failed || tuning_cache_.init_progress > 0.0f ||
        !tuning_cache_.init_status_text.empty())
    {
        ImGui::Text("初始化状态");
        if (tuning_cache_.init_failed)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 90, 90, 255));
            ImGui::TextWrapped("%s", tuning_cache_.init_status_text.c_str());
            ImGui::PopStyleColor();
        }
        else
        {
            const char* status_text = tuning_cache_.init_status_text.empty()
                ? (tuning_cache_.init_in_progress ? "初始化中..." : "初始化完成")
                : tuning_cache_.init_status_text.c_str();
            ImGui::TextWrapped("%s", status_text);
        }
                float shown_progress = tuning_cache_.init_progress;
        if (tuning_cache_.init_in_progress && shown_progress < 0.0f)
        {
            shown_progress =
                0.2f + 0.6f * (0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime() * 2.2)));
            ImGui::TextUnformatted("进度条为活动指示，不代表真实百分比");
        }
        ImGui::ProgressBar(std::clamp(shown_progress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
        ImGui::Separator();
    }

    ImGui::Checkbox("开启自瞄", &tuning_cache_.aim_enabled);
    ImGui::Checkbox("开启可视化窗口", &tuning_cache_.preview_enabled);
    ImGui::Checkbox("开启性能日志", &tuning_cache_.verbose_log_enabled);
    ImGui::Separator();

    ImGui::SliderFloat("置信度阈值", &tuning_cache_.conf_threshold, 0.01f, 1.00f, "%.2f");
    ImGui::SliderFloat("NMS IoU 阈值", &tuning_cache_.nms_iou_threshold, 0.01f, 1.00f, "%.2f");
    ImGui::SliderFloat("中心激活半径(px)", &tuning_cache_.active_circle_radius_px, 10.0f, 400.0f, "%.0f");
    ImGui::SliderFloat("自瞄平滑系数", &tuning_cache_.aim_smooth_factor, 0.10f, 2.00f, "%.2f");
    ImGui::SliderFloat("最大单步移动(px)", &tuning_cache_.aim_max_step_px, 1.0f, 500.0f, "%.0f");
    ImGui::SliderFloat("瞄准死区(px)", &tuning_cache_.aim_deadzone_px, 0.0f, 10.0f, "%.1f");
    ImGui::Separator();

    if (ImGui::Button("退出程序", ImVec2(160.0f, 0.0f)))
    {
        tuning_cache_.request_exit = true;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);

    ApplyRuntimeLimits(tuning_cache_);
    tuning = tuning_cache_;

    ImGui::Render();
    const float clear_color[4] = { 0.08f, 0.08f, 0.10f, 1.00f };
    d3d_device_context_->OMSetRenderTargets(1, &render_target_view_, nullptr);
    d3d_device_context_->ClearRenderTargetView(render_target_view_, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swap_chain_->Present(1, 0);
}

void ControlPanel::ApplyRuntimeLimits(RuntimeTuning& tuning) const
{
    tuning.conf_threshold = std::clamp(tuning.conf_threshold, 0.01f, 1.00f);
    tuning.nms_iou_threshold = std::clamp(tuning.nms_iou_threshold, 0.01f, 1.00f);
    tuning.active_circle_radius_px = std::clamp(tuning.active_circle_radius_px, 10.0f, 400.0f);
    tuning.aim_smooth_factor = std::clamp(tuning.aim_smooth_factor, 0.10f, 2.00f);
    tuning.aim_max_step_px = std::clamp(tuning.aim_max_step_px, 1.0f, 500.0f);
    tuning.aim_deadzone_px = std::clamp(tuning.aim_deadzone_px, 0.0f, 10.0f);
}

LRESULT CALLBACK ControlPanel::WndProc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, w_param, l_param))
    {
        return TRUE;
    }

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTA* create = reinterpret_cast<CREATESTRUCTA*>(l_param);
        if (create != nullptr)
        {
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        return DefWindowProcA(hwnd, msg, w_param, l_param);
    }

    auto* self = reinterpret_cast<ControlPanel*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (self == nullptr)
    {
        return DefWindowProcA(hwnd, msg, w_param, l_param);
    }

    switch (msg)
    {
    case WM_SIZE:
        if (w_param != SIZE_MINIMIZED)
        {
            self->HandleResize(static_cast<unsigned int>(LOWORD(l_param)), static_cast<unsigned int>(HIWORD(l_param)));
        }
        return 0;
    case WM_CLOSE:
        self->tuning_cache_.request_exit = true;
        return 0;
    default:
        break;
    }

    return DefWindowProcA(hwnd, msg, w_param, l_param);
}

} // namespace aim

