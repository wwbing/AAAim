#include "ort_trt_infer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <array>
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
    if (config::kEnableVerboseLog)
    {
        std::cout << "[初始化] ORT 开始初始化, 模型=" << model_path << "\n";
    }

    const std::wstring model_path_w = ToWideString(model_path);
    session_.reset();
    backend_name_ = "CPU";
    trt_cache_dir_.clear();

    auto make_default_options = []() {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        return options;
    };

    if (config::kPreferTensorRt)
    {
        try
        {
            Ort::SessionOptions trt_options = make_default_options();
            if (AppendTensorRtProvider(trt_options))
            {
                if (config::kEnableVerboseLog)
                {
                    std::cout << "[初始化] 正在创建 ORT Session（TensorRT）...\n";
                }
                if (CreateSessionWithOptions(model_path_w, trt_options, "TensorRT"))
                {
                    return true;
                }
            }
        }
        catch (const Ort::Exception& ex)
        {
            if (config::kEnableVerboseLog)
            {
                std::cerr << "[初始化] TensorRT 创建异常: " << ex.what() << "\n";
            }
        }
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] TensorRT 初始化失败，准备回退到 CUDA。\n";
        }
    }

    if (config::kEnableCudaFallback)
    {
        try
        {
            Ort::SessionOptions cuda_options = make_default_options();
            if (AppendCudaProvider(cuda_options))
            {
                if (config::kEnableVerboseLog)
                {
                    std::cout << "[初始化] 正在创建 ORT Session（CUDA）...\n";
                }
                if (CreateSessionWithOptions(model_path_w, cuda_options, "CUDA"))
                {
                    return true;
                }
            }
        }
        catch (const Ort::Exception& ex)
        {
            if (config::kEnableVerboseLog)
            {
                std::cerr << "[初始化] CUDA 创建异常: " << ex.what() << "\n";
            }
        }
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] CUDA 初始化失败。\n";
        }
    }

    if (config::kEnableCpuFallback)
    {
        try
        {
            Ort::SessionOptions cpu_options = make_default_options();
            if (config::kEnableVerboseLog)
            {
                std::cout << "[初始化] 正在创建 ORT Session（CPU 回退）...\n";
            }
            if (CreateSessionWithOptions(model_path_w, cpu_options, "CPU"))
            {
                return true;
            }
        }
        catch (const Ort::Exception& ex)
        {
            if (config::kEnableVerboseLog)
            {
                std::cerr << "[初始化] CPU 创建异常: " << ex.what() << "\n";
            }
        }
    }

    return false;
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

bool OrtTrtInfer::CreateSessionWithOptions(
    const std::wstring& model_path_w,
    Ort::SessionOptions& session_options,
    const std::string& backend_name)
{
    session_ = std::make_unique<Ort::Session>(env_, model_path_w.c_str(), session_options);
    backend_name_ = backend_name;
    return RefreshInputOutputMetadata();
}

bool OrtTrtInfer::RefreshInputOutputMetadata()
{
    if (!session_)
    {
        return false;
    }

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
        std::cout << "[初始化] Session 就绪。后端=" << backend_name_
                  << ", 输入数=" << input_count
                  << ", 输出数=" << output_count;
        if (!trt_cache_dir_.empty())
        {
            std::cout << ", TRT缓存目录=" << trt_cache_dir_;
        }
        std::cout << "\n";
    }

    return !input_name_ptrs_.empty() && !output_name_ptrs_.empty();
}

bool OrtTrtInfer::AppendTensorRtProvider(Ort::SessionOptions& session_options)
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
    push_opt("trt_build_heuristics_enable", config::kTrtBuildHeuristicsEnable ? "1" : "0");
    push_opt("trt_min_subgraph_size", std::to_string(config::kTrtMinSubgraphSize));
    push_opt("trt_max_partition_iterations", std::to_string(config::kTrtMaxPartitionIterations));

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

    status = api.SessionOptionsAppendExecutionProvider_TensorRT_V2(session_options, trt_options);
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

bool OrtTrtInfer::AppendCudaProvider(Ort::SessionOptions& session_options)
{
    const OrtApi& api = Ort::GetApi();
    OrtCUDAProviderOptionsV2* cuda_options = nullptr;

    OrtStatus* status = api.CreateCUDAProviderOptions(&cuda_options);
    if (status != nullptr || cuda_options == nullptr)
    {
        const std::string err = OrtStatusToString(status);
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] CreateCUDAProviderOptions 失败: " << err << "\n";
        }
        return false;
    }

    std::vector<const char*> keys;
    std::vector<std::string> values_str;
    auto push_opt = [&](const char* key, const std::string& value) {
        keys.push_back(key);
        values_str.push_back(value);
    };

    push_opt("device_id", "0");

    std::vector<const char*> values;
    values.reserve(values_str.size());
    for (const auto& v : values_str)
    {
        values.push_back(v.c_str());
    }

    status = api.UpdateCUDAProviderOptions(cuda_options, keys.data(), values.data(), keys.size());
    if (status != nullptr)
    {
        api.ReleaseCUDAProviderOptions(cuda_options);
        const std::string err = OrtStatusToString(status);
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] UpdateCUDAProviderOptions 失败: " << err << "\n";
        }
        return false;
    }

    status = api.SessionOptionsAppendExecutionProvider_CUDA_V2(session_options, cuda_options);
    api.ReleaseCUDAProviderOptions(cuda_options);
    if (status != nullptr)
    {
        const std::string err = OrtStatusToString(status);
        if (config::kEnableVerboseLog)
        {
            std::cerr << "[初始化] 挂载 CUDA 执行器失败: " << err << "\n";
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
