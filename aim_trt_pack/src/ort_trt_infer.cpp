#include "ort_trt_infer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/dnn.hpp>
#include "app_config.h"

namespace {

std::string DirnameOf(const std::string& path)
{
    const size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

} // namespace

namespace aim {

OrtTrtInfer::OrtTrtInfer(int input_size)
    : input_size_(input_size),
      env_(ORT_LOGGING_LEVEL_WARNING, "aim_trt")
{
}

bool OrtTrtInfer::Initialize(const std::string& model_path)
{
    try
    {
        if (config::kEnableVerboseLog)
        {
            std::cout << "[初始化] ORT 开始初始化, 模型=" << model_path << "\n";
        }

        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        if (!AppendTensorRtProvider())
        {
            if (config::kEnableVerboseLog)
            {
                std::cout << "[初始化] 挂载 TensorRT Provider 失败。\n";
            }
            return false;
        }
        backend_name_ = "TensorRT";

        if (config::kEnableVerboseLog)
        {
            std::cout << "[初始化] 正在创建 ORT Session（首次会构建 TRT 引擎，耗时会较长）...\n";
        }

        const std::wstring model_path_w = ToWideString(model_path);
        session_ = std::make_unique<Ort::Session>(env_, model_path_w.c_str(), session_options_);

        Ort::AllocatorWithDefaultOptions allocator;

        const size_t input_count = session_->GetInputCount();
        input_names_.clear();
        input_name_ptrs_.clear();
        input_names_.reserve(input_count);
        input_name_ptrs_.reserve(input_count);
        for (size_t i = 0; i < input_count; ++i)
        {
            auto name = session_->GetInputNameAllocated(i, allocator);
            input_names_.push_back(name ? name.get() : "");
            input_name_ptrs_.push_back(input_names_.back().c_str());
        }

        const size_t output_count = session_->GetOutputCount();
        output_names_.clear();
        output_name_ptrs_.clear();
        output_names_.reserve(output_count);
        output_name_ptrs_.reserve(output_count);
        for (size_t i = 0; i < output_count; ++i)
        {
            auto name = session_->GetOutputNameAllocated(i, allocator);
            output_names_.push_back(name ? name.get() : "");
            output_name_ptrs_.push_back(output_names_.back().c_str());
        }

        if (config::kEnableVerboseLog)
        {
            std::cout << "[初始化] Session 就绪。输入数=" << input_count
                      << ", 输出数=" << output_count
                      << ", TRT缓存目录=" << trt_cache_dir_ << "\n";
        }

        return !input_name_ptrs_.empty() && !output_name_ptrs_.empty();
    }
    catch (const Ort::Exception& ex)
    {
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] ORT 异常: " << ex.what() << "\n";
        }
        return false;
    }
}

bool OrtTrtInfer::Run(const cv::Mat& bgr, RawTensor& output)
{
    output.data.clear();
    output.shape.clear();

    if (!session_ || bgr.empty())
    {
        return false;
    }

    cv::Mat blob = cv::dnn::blobFromImage(
        bgr,
        1.0f / 255.0f,
        cv::Size(input_size_, input_size_),
        cv::Scalar(),
        true,
        false,
        CV_32F);

    if (!blob.isContinuous())
    {
        blob = blob.clone();
    }

    const std::array<int64_t, 4> input_shape = { 1, 3, input_size_, input_size_ };
    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        mem_info,
        reinterpret_cast<float*>(blob.data),
        blob.total(),
        input_shape.data(),
        input_shape.size());

    std::vector<Ort::Value> outputs = session_->Run(
        Ort::RunOptions{ nullptr },
        input_name_ptrs_.data(),
        &input_tensor,
        1,
        output_name_ptrs_.data(),
        output_name_ptrs_.size());

    if (outputs.empty() || !outputs[0].IsTensor())
    {
        return false;
    }

    const Ort::Value& first = outputs[0];
    const auto info = first.GetTensorTypeAndShapeInfo();
    const size_t elem_count = info.GetElementCount();
    if (elem_count == 0)
    {
        return false;
    }

    output.shape = info.GetShape();
    const float* data = first.GetTensorData<float>();
    output.data.assign(data, data + elem_count);
    return true;
}

bool OrtTrtInfer::AppendTensorRtProvider()
{
    const OrtApi& api = Ort::GetApi();
    OrtTensorRTProviderOptionsV2* trt_options = nullptr;

    OrtStatus* status = api.CreateTensorRTProviderOptions(&trt_options);
    if (status != nullptr || trt_options == nullptr)
    {
        const std::string err = OrtStatusToString(status);
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] CreateTensorRTProviderOptions 失败: " << err << "\n";
        }
        return false;
    }

    trt_cache_dir_ = CurrentExeDir() + "\\trt_cache";
    std::error_code ec;
    std::filesystem::create_directories(trt_cache_dir_, ec);

    std::vector<const char*> keys;
    std::vector<std::string> values_str;
    auto push_opt = [&](const char* key, const std::string& value) {
        keys.push_back(key);
        values_str.push_back(value);
    };

    push_opt("device_id", "0");
    push_opt("trt_fp16_enable", config::kTrtFp16Enable ? "1" : "0");
    push_opt("trt_engine_cache_enable", config::kTrtEngineCacheEnable ? "1" : "0");
    push_opt("trt_force_sequential_engine_build", config::kTrtForceSequentialEngineBuild ? "1" : "0");
    push_opt("trt_builder_optimization_level", std::to_string(config::kTrtBuilderOptimizationLevel));
    if (config::kTrtEngineCacheEnable)
    {
        push_opt("trt_engine_cache_path", trt_cache_dir_);
    }
    push_opt("trt_timing_cache_enable", config::kTrtTimingCacheEnable ? "1" : "0");
    if (config::kTrtTimingCacheEnable)
    {
        push_opt("trt_timing_cache_path", trt_cache_dir_);
    }

    std::vector<const char*> values;
    values.reserve(values_str.size());
    for (const auto& v : values_str)
    {
        values.push_back(v.c_str());
    }

    status = api.UpdateTensorRTProviderOptions(trt_options, keys.data(), values.data(), keys.size());
    if (status != nullptr)
    {
        api.ReleaseTensorRTProviderOptions(trt_options);
        const std::string err = OrtStatusToString(status);
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] UpdateTensorRTProviderOptions 失败: " << err << "\n";
        }
        return false;
    }

    status = api.SessionOptionsAppendExecutionProvider_TensorRT_V2(session_options_, trt_options);
    api.ReleaseTensorRTProviderOptions(trt_options);
    if (status != nullptr)
    {
        const std::string err = OrtStatusToString(status);
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] 挂载 TensorRT 执行器失败: " << err << "\n";
        }
        return false;
    }

    return true;
}

std::string OrtTrtInfer::OrtStatusToString(OrtStatus* status)
{
    if (status == nullptr)
    {
        return "";
    }
    const OrtApi& api = Ort::GetApi();
    const char* msg = api.GetErrorMessage(status);
    std::string out = msg ? msg : "unknown OrtStatus error";
    api.ReleaseStatus(status);
    return out;
}

std::wstring OrtTrtInfer::ToWideString(const std::string& text)
{
    UINT codepage = CP_UTF8;
    int required = MultiByteToWideChar(codepage, 0, text.c_str(), -1, nullptr, 0);
    if (required <= 0)
    {
        codepage = CP_ACP;
        required = MultiByteToWideChar(codepage, 0, text.c_str(), -1, nullptr, 0);
    }
    if (required <= 0)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring out(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(codepage, 0, text.c_str(), -1, out.data(), required);
    if (!out.empty() && out.back() == L'\0')
    {
        out.pop_back();
    }
    return out;
}

std::string OrtTrtInfer::CurrentExeDir()
{
    char exe_path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0)
    {
        return DirnameOf(std::string(exe_path));
    }
    return ".";
}

} // namespace aim
