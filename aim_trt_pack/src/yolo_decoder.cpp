#include "yolo_decoder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aim {

YoloDecoder::YoloDecoder(float conf_threshold, float nms_iou_threshold)
    : conf_threshold_(conf_threshold),
      nms_iou_threshold_(nms_iou_threshold)
{
}

void YoloDecoder::SetThresholds(float conf_threshold, float nms_iou_threshold)
{
    conf_threshold_ = std::clamp(conf_threshold, 0.01f, 1.00f);
    nms_iou_threshold_ = std::clamp(nms_iou_threshold, 0.01f, 1.00f);
}

void YoloDecoder::Decode(const RawTensor& output, Detections& detections) const
{
    detections.clear();
    if (output.data.empty() || output.shape.empty())
    {
        return;
    }

    const float* data = output.data.data();
    const auto& shape = output.shape;
    const size_t elem_count = output.data.size();

    auto push_xywh_if_keep = [&](float x, float y, float w, float h, float conf, float cls_id) {
        if (conf < conf_threshold_)
        {
            return;
        }
        const float x1 = x - w * 0.5f;
        const float y1 = y - h * 0.5f;
        const float x2 = x + w * 0.5f;
        const float y2 = y + h * 0.5f;
        detections.push_back({ x1, y1, x2, y2, conf, cls_id });
    };

    auto decode_row = [&](const float* row, size_t cols) {
        if (cols < 6)
        {
            return;
        }

        float conf = row[4];
        float cls_id = row[5];

        if (cols > 6)
        {
            const float obj = row[4];
            float best_cls = row[5];
            size_t best_idx = 0;
            for (size_t c = 6; c < cols; ++c)
            {
                if (row[c] > best_cls)
                {
                    best_cls = row[c];
                    best_idx = c - 5;
                }
            }
            conf = obj * best_cls;
            cls_id = static_cast<float>(best_idx);
        }

        push_xywh_if_keep(row[0], row[1], row[2], row[3], conf, cls_id);
    };

    auto decode_chw = [&](const float* base, size_t channels, size_t n) {
        if (channels < 6)
        {
            return;
        }

        for (size_t i = 0; i < n; ++i)
        {
            float conf = base[4 * n + i];
            float cls_id = base[5 * n + i];

            if (channels > 6)
            {
                const float obj = base[4 * n + i];
                float best_cls = base[5 * n + i];
                size_t best_idx = 0;
                for (size_t c = 6; c < channels; ++c)
                {
                    const float cls_score = base[c * n + i];
                    if (cls_score > best_cls)
                    {
                        best_cls = cls_score;
                        best_idx = c - 5;
                    }
                }
                conf = obj * best_cls;
                cls_id = static_cast<float>(best_idx);
            }

            push_xywh_if_keep(
                base[0 * n + i],
                base[1 * n + i],
                base[2 * n + i],
                base[3 * n + i],
                conf,
                cls_id);
        }
    };

    if (shape.size() == 3)
    {
        const size_t d0 = static_cast<size_t>(shape[0] < 1 ? 1 : shape[0]);
        const size_t d1 = static_cast<size_t>(shape[1] < 1 ? 1 : shape[1]);
        const size_t d2 = static_cast<size_t>(shape[2] < 1 ? 1 : shape[2]);

        if (d1 >= 6 && d1 < d2) // [N, C, num]
        {
            detections.reserve(d0 * d2);
            for (size_t b = 0; b < d0; ++b)
            {
                const float* base = data + b * d1 * d2;
                decode_chw(base, d1, d2);
            }
            ApplyNms(detections);
            return;
        }

        if (d2 >= 6) // [N, num, C]
        {
            detections.reserve(d0 * d1);
            const size_t rows = d0 * d1;
            for (size_t i = 0; i < rows; ++i)
            {
                decode_row(data + i * d2, d2);
            }
            ApplyNms(detections);
            return;
        }
    }

    if (shape.size() == 2)
    {
        const size_t r = static_cast<size_t>(shape[0] < 1 ? 1 : shape[0]);
        const size_t c = static_cast<size_t>(shape[1] < 1 ? 1 : shape[1]);
        if (c >= 6)
        {
            detections.reserve(r);
            for (size_t i = 0; i < r; ++i)
            {
                decode_row(data + i * c, c);
            }
            ApplyNms(detections);
            return;
        }
        if (r >= 6)
        {
            detections.reserve(c);
            decode_chw(data, r, c);
            ApplyNms(detections);
            return;
        }
    }

    if ((elem_count % 6) == 0)
    {
        const size_t rows = elem_count / 6;
        detections.reserve(rows);
        for (size_t i = 0; i < rows; ++i)
        {
            decode_row(data + i * 6, 6);
        }
    }

    ApplyNms(detections);
}

bool YoloDecoder::SelectNearestTarget(
    const Detections& detections,
    float capture_offset_x,
    float capture_offset_y,
    float screen_center_x,
    float screen_center_y,
    int target_class_id,
    TargetPoint& target) const
{
    target = {};
    if (detections.empty())
    {
        return false;
    }

    float best_dist2 = std::numeric_limits<float>::max();

    for (const auto& det : detections)
    {
        if (target_class_id >= 0 && static_cast<int>(det[5]) != target_class_id)
        {
            continue;
        }

        const float local_cx = (det[0] + det[2]) * 0.5f;
        const float local_cy = (det[1] + det[3]) * 0.5f;
        const float global_x = local_cx + capture_offset_x;
        const float global_y = local_cy + capture_offset_y;

        const float dx = global_x - screen_center_x;
        const float dy = global_y - screen_center_y;
        const float dist2 = dx * dx + dy * dy;

        if (dist2 < best_dist2)
        {
            best_dist2 = dist2;
            target.x = global_x;
            target.y = global_y;
        }
    }

    if (best_dist2 == std::numeric_limits<float>::max())
    {
        return false;
    }

    target.distance = std::sqrt(best_dist2);
    target.valid = true;
    return true;
}

void YoloDecoder::ApplyNms(Detections& detections) const
{
    std::sort(
        detections.begin(),
        detections.end(),
        [](const Detection& a, const Detection& b) { return a[4] > b[4]; });

    Detections filtered;
    filtered.reserve(detections.size());

    for (const auto& box : detections)
    {
        bool keep = true;
        for (const auto& fbox : filtered)
        {
            const float x1 = std::max(box[0], fbox[0]);
            const float y1 = std::max(box[1], fbox[1]);
            const float x2 = std::min(box[2], fbox[2]);
            const float y2 = std::min(box[3], fbox[3]);
            const float inter_area = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
            const float union_area =
                (box[2] - box[0]) * (box[3] - box[1]) +
                (fbox[2] - fbox[0]) * (fbox[3] - fbox[1]) -
                inter_area;

            if (union_area > 0.0f && (inter_area / union_area) > nms_iou_threshold_)
            {
                keep = false;
                break;
            }
        }

        if (keep)
        {
            filtered.push_back(box);
        }
    }

    detections = std::move(filtered);
}

} // namespace aim
