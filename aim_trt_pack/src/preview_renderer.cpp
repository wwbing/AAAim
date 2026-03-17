#include "preview_renderer.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace {

constexpr int kClassHead = 0;
const std::vector<std::string> kClassNames = { "head" };

cv::Scalar ClassColor(int cls_idx)
{
    switch (cls_idx)
    {
    case kClassHead:
        return cv::Scalar(0, 255, 0);
    default:
        return cv::Scalar(180, 180, 180);
    }
}

} // namespace

namespace aim {

int HeadClassId()
{
    return kClassHead;
}

const std::vector<std::string>& ClassNames()
{
    return kClassNames;
}

const char* TargetClassName(int target_class_id)
{
    return target_class_id == kClassHead ? "HEAD" : "UNKNOWN";
}

void DrawPreview(
    cv::Mat& frame,
    const Detections& detections,
    bool aim_enabled,
    int target_class_id,
    float active_radius_px)
{
    const int cx = frame.cols / 2;
    const int cy = frame.rows / 2;
    const int radius = static_cast<int>(std::lround(std::max(1.0f, active_radius_px)));
    cv::circle(frame, cv::Point(cx, cy), radius, cv::Scalar(0, 255, 255), 1);

    for (const auto& det : detections)
    {
        const cv::Point p1(static_cast<int>(det[0]), static_cast<int>(det[1]));
        const cv::Point p2(static_cast<int>(det[2]), static_cast<int>(det[3]));
        const int cls_idx = static_cast<int>(det[5]);
        const cv::Scalar cls_color = ClassColor(cls_idx);
        const int thickness = (cls_idx == target_class_id) ? 3 : 2;
        cv::rectangle(frame, p1, p2, cls_color, thickness);
        const std::string cls_name =
            (cls_idx >= 0 && cls_idx < static_cast<int>(kClassNames.size())) ? kClassNames[cls_idx] : "obj";
        cv::putText(
            frame,
            cls_name + " " + std::to_string(det[4]),
            p1,
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cls_color,
            1);
    }

    const std::string status = std::string("自瞄: ") + (aim_enabled ? "开" : "关") +
        " | 目标: " + TargetClassName(target_class_id);
    cv::putText(
        frame,
        status,
        cv::Point(10, 24),
        cv::FONT_HERSHEY_SIMPLEX,
        0.6,
        cv::Scalar(0, 255, 255),
        2);

    int legend_y = 48;
    for (int cls_idx = 0; cls_idx < static_cast<int>(kClassNames.size()); ++cls_idx)
    {
        const cv::Scalar cls_color = ClassColor(cls_idx);
        cv::rectangle(frame, cv::Point(10, legend_y - 12), cv::Point(28, legend_y + 4), cls_color, cv::FILLED);
        cv::putText(
            frame,
            kClassNames[cls_idx],
            cv::Point(34, legend_y),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cls_color,
            1);
        legend_y += 20;
    }
}

} // namespace aim

