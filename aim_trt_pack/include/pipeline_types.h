#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace aim {

struct RawTensor {
    std::vector<float> data;
    std::vector<int64_t> shape;
};

using Detection = std::array<float, 6>; // [x1, y1, x2, y2, conf, cls]
using Detections = std::vector<Detection>;

struct TargetPoint {
    float x = 0.0f;
    float y = 0.0f;
    float distance = 0.0f;
    bool valid = false;
};

} // namespace aim

