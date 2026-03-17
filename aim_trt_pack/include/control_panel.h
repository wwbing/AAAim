#pragma once

#include "runtime_tuning.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace aim {

class ControlPanel {
public:
    bool Initialize(const RuntimeTuning& tuning);
    void Destroy();
    void SetVisible(bool visible);
    bool IsVisible() const { return visible_; }
    void SyncFromRuntime(const RuntimeTuning& tuning);
    void Poll(RuntimeTuning& tuning);

private:
    bool CreateWindowAndDevice();
    bool CreateD3DDevice();
    void CleanupD3DDevice();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void HandleResize(unsigned int width, unsigned int height);
    void RenderUi(RuntimeTuning& tuning);
    void ApplyRuntimeLimits(RuntimeTuning& tuning) const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);

private:
    bool initialized_ = false;
    bool visible_ = false;

    RuntimeTuning tuning_cache_{};

    HWND hwnd_ = nullptr;
    WNDCLASSEXA window_class_ = {};
    ID3D11Device* d3d_device_ = nullptr;
    ID3D11DeviceContext* d3d_device_context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    ID3D11RenderTargetView* render_target_view_ = nullptr;
};

} // namespace aim
