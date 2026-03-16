#pragma once

#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

#include "pipeline_types.h"

namespace aim {

class OrtTrtInfer {
public:
    explicit OrtTrtInfer(int input_size);

    bool Initialize(const std::string& model_path);
    bool Run(const cv::Mat& bgr, RawTensor& output);

    const std::string& BackendName() const { return backend_name_; }

private:
    bool AppendTensorRtProvider();
    static std::string OrtStatusToString(OrtStatus* status);
    static std::wstring ToWideString(const std::string& text);
    static std::string CurrentExeDir();

private:
    int input_size_ = 640;
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::string backend_name_ = "CPU";
    std::string trt_cache_dir_;
    std::vector<std::string> input_names_;
    std::vector<const char*> input_name_ptrs_;
    std::vector<std::string> output_names_;
    std::vector<const char*> output_name_ptrs_;
};

} // namespace aim

