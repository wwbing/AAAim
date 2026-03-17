#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "pipeline_types.h"

namespace aim {

int HeadClassId();
const std::vector<std::string>& ClassNames();
const char* TargetClassName(int target_class_id);

void DrawPreview(
    cv::Mat& frame,
    const Detections& detections,
    bool aim_enabled,
    int target_class_id,
    float active_radius_px);

} // namespace aim

