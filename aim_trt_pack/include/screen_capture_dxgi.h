#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdexcept>
#include <opencv2/opencv.hpp>
#include "app_config.h"

template<typename T>
inline void SafeRelease(T** ppT)
{
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

class ScreenCapturer {
public:
    ScreenCapturer(
        int crop_width = aim::config::kCaptureSize,
        int crop_height = aim::config::kCaptureSize)
        : crop_width_(crop_width), crop_height_(crop_height)
    {
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pFactory));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create DXGI factory.");
        }
        if (FAILED(pFactory->EnumAdapters1(0, &pAdapter))) {
            throw std::runtime_error("Failed to enumerate adapter.");
        }
        if (FAILED(pAdapter->EnumOutputs(0, &pOutput))) {
            throw std::runtime_error("Failed to enumerate output.");
        }

        DXGI_OUTPUT_DESC output_desc = {};
        pOutput->GetDesc(&output_desc);
        width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
        height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;

        if (width < crop_width_ || height < crop_height_) {
            throw std::runtime_error("Screen resolution is smaller than capture size.");
        }

        hr = D3D11CreateDevice(
            pAdapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &pDevice,
            nullptr,
            &pContext);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create D3D11 device.");
        }

        if (FAILED(pOutput->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&pOutput1)))) {
            throw std::runtime_error("Failed to query IDXGIOutput1.");
        }
        if (FAILED(pOutput1->DuplicateOutput(pDevice, &pDuplicator))) {
            throw std::runtime_error("Failed to duplicate output.");
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(crop_width_);
        desc.Height = static_cast<UINT>(crop_height_);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        hr = pDevice->CreateTexture2D(&desc, nullptr, &staging_texture_);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create staging texture.");
        }
    }

    ~ScreenCapturer()
    {
        SafeRelease(&staging_texture_);
        SafeRelease(&pDuplicator);
        SafeRelease(&pOutput1);
        SafeRelease(&pOutput);
        SafeRelease(&pAdapter);
        SafeRelease(&pFactory);
        SafeRelease(&pContext);
        SafeRelease(&pDevice);
    }

    ScreenCapturer(const ScreenCapturer&) = delete;
    ScreenCapturer& operator=(const ScreenCapturer&) = delete;

    bool CaptureFrame(cv::Mat& frame)
    {
        IDXGIResource* desktop_resource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frame_info = {};
        HRESULT hr = pDuplicator->AcquireNextFrame(
            static_cast<UINT>(aim::config::kDxgiAcquireTimeoutMs),
            &frame_info,
            &desktop_resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return false;
        }
        if (FAILED(hr)) {
            return false;
        }

        ID3D11Texture2D* desktop_image = nullptr;
        hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&desktop_image));
        SafeRelease(&desktop_resource);
        if (FAILED(hr)) {
            pDuplicator->ReleaseFrame();
            return false;
        }

        const int start_x = (width - crop_width_) / 2;
        const int start_y = (height - crop_height_) / 2;

        D3D11_BOX source_region = {};
        source_region.left = static_cast<UINT>(start_x);
        source_region.top = static_cast<UINT>(start_y);
        source_region.right = static_cast<UINT>(start_x + crop_width_);
        source_region.bottom = static_cast<UINT>(start_y + crop_height_);
        source_region.front = 0;
        source_region.back = 1;

        pContext->CopySubresourceRegion(staging_texture_, 0, 0, 0, 0, desktop_image, 0, &source_region);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = pContext->Map(staging_texture_, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            SafeRelease(&desktop_image);
            pDuplicator->ReleaseFrame();
            return false;
        }

        cv::Mat bgra(crop_height_, crop_width_, CV_8UC4, mapped.pData, mapped.RowPitch);
        cv::cvtColor(bgra, frame, cv::COLOR_BGRA2BGR);

        pContext->Unmap(staging_texture_, 0);
        SafeRelease(&desktop_image);
        pDuplicator->ReleaseFrame();
        return true;
    }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    int crop_width_ = aim::config::kCaptureSize;
    int crop_height_ = aim::config::kCaptureSize;
    int width = 0;
    int height = 0;

    IDXGIFactory1* pFactory = nullptr;
    IDXGIAdapter1* pAdapter = nullptr;
    IDXGIOutput* pOutput = nullptr;
    IDXGIOutput1* pOutput1 = nullptr;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    IDXGIOutputDuplication* pDuplicator = nullptr;
    ID3D11Texture2D* staging_texture_ = nullptr;
};
