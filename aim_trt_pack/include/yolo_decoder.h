#pragma once

#include "pipeline_types.h"

namespace aim {

class YoloDecoder {
public:
    YoloDecoder(float conf_threshold, float nms_iou_threshold);

    void Decode(const RawTensor& output, Detections& detections) const;
    bool SelectNearestTarget(
        const Detections& detections,
        float capture_offset_x,
        float capture_offset_y,
        float screen_center_x,
        float screen_center_y,
        TargetPoint& target) const;

private:
    void ApplyNms(Detections& detections) const;

private:
    float conf_threshold_ = 0.25f;
    float nms_iou_threshold_ = 0.6f;
};

} // namespace aim

