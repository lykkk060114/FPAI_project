#include "detection/Yolo26Postprocess.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace xunjing::detection
{
namespace
{
float IntersectionOverUnion(const cv::Rect2f& a, const cv::Rect2f& b)
{
    const float left = std::max(a.x, b.x);
    const float top = std::max(a.y, b.y);
    const float right = std::min(a.x + a.width, b.x + b.width);
    const float bottom = std::min(a.y + a.height, b.y + b.height);
    const float intersection = std::max(0.0F, right - left) *
        std::max(0.0F, bottom - top);
    const float total = a.area() + b.area() - intersection;
    return total > 0.0F ? intersection / total : 0.0F;
}

std::vector<Detection> ClassAwareNms(std::vector<Detection> detections,
                                     float iou_threshold, std::size_t limit = static_cast<std::size_t>(-1))
{
    if (limit == 0) return {};
    std::stable_sort(detections.begin(), detections.end(),
                     [](const Detection& a, const Detection& b) {
                         return a.confidence > b.confidence;
                     });
    std::vector<Detection> selected;
    std::vector<bool> removed(detections.size(), false);
    for (std::size_t i = 0; i < detections.size(); ++i) {
        if (removed[i]) {
            continue;
        }
        selected.push_back(detections[i]);
        if (selected.size() >= limit) break;
        for (std::size_t j = i + 1; j < detections.size(); ++j) {
            if (!removed[j] && detections[i].class_id == detections[j].class_id &&
                IntersectionOverUnion(detections[i].box, detections[j].box) >
                    iou_threshold) {
                removed[j] = true;
            }
        }
    }
    return selected;
}
} // namespace

std::vector<Detection> DecodeYolo26Conv(
    const std::vector<Yolo26OutputView> &outputs,
    int class_count,
    float confidence_threshold,
    float iou_threshold,
    std::size_t max_detections,
    std::size_t* candidates_before_nms)
{
    if (outputs.size() != 6 || class_count != 1)
        throw std::invalid_argument("YOLO26 Conv decoder expects six outputs and one class");
    if (!std::isfinite(iou_threshold) || iou_threshold < 0.0F || iou_threshold > 1.0F)
        throw std::invalid_argument("NMS IoU threshold must be in [0,1]");
    std::vector<Detection> candidates;
    for (std::size_t level = 0; level < 3; ++level) {
        const auto &cls = outputs[level * 2];
        const auto &box = outputs[level * 2 + 1];
        if (cls.data == nullptr || box.data == nullptr ||
            cls.shape.size() != 4 || box.shape.size() != 4 ||
            cls.shape[0] != 1 || box.shape[0] != 1 ||
            cls.shape[1] != box.shape[1] || cls.shape[2] != box.shape[2] ||
            cls.shape[3] != 1 || box.shape[3] != 4)
            throw std::invalid_argument("invalid YOLO26 Conv output shape");
        const int height = static_cast<int>(cls.shape[1]);
        const int width = static_cast<int>(cls.shape[2]);
        const float stride = static_cast<float>(8 << level);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t cls_index =
                    (static_cast<std::size_t>(y) * width + x);
                const float logit = cls.data[cls_index];
                const float score = 1.0F / (1.0F + std::exp(-std::clamp(logit, -80.0F, 80.0F)));
                if (!std::isfinite(score) || score < confidence_threshold)
                    continue;
                const std::size_t box_index = cls_index * 4;
                const float left = (static_cast<float>(x) + 0.5F - box.data[box_index]) * stride;
                const float top = (static_cast<float>(y) + 0.5F - box.data[box_index + 1]) * stride;
                const float right = (static_cast<float>(x) + 0.5F + box.data[box_index + 2]) * stride;
                const float bottom = (static_cast<float>(y) + 0.5F + box.data[box_index + 3]) * stride;
                const float w = right - left;
                const float h = bottom - top;
                if (std::isfinite(left) && std::isfinite(top) &&
                    std::isfinite(w) && std::isfinite(h) && w >= 1.0F && h >= 1.0F)
                    candidates.push_back({0, score, {left, top, w, h}});
            }
        }
    }
    if (candidates_before_nms) *candidates_before_nms = candidates.size();
    return ClassAwareNms(std::move(candidates), iou_threshold, max_detections);
}

std::vector<Detection> DecodeYolo26(const float* data,
                                    const std::vector<int64_t>& shape,
                                    int class_count,
                                    float confidence_threshold,
                                    float iou_threshold)
{
    if (data == nullptr || shape.size() != 3 || shape[0] != 1 ||
        class_count <= 0) {
        throw std::invalid_argument("invalid YOLO26 tensor or class count");
    }

    std::vector<Detection> decoded;
    if (shape[2] == 6 && shape[1] > 0) {
        // End-to-end one-to-one head. It is already duplicate-free.
        const std::size_t rows = static_cast<std::size_t>(shape[1]);
        for (std::size_t i = 0; i < rows; ++i) {
            const float* row = data + i * 6;
            const float score = row[4];
            const int class_id = static_cast<int>(std::lround(row[5]));
            if (!std::isfinite(score) || score < confidence_threshold ||
                class_id < 0 || class_id >= class_count) {
                continue;
            }
            const float width = row[2] - row[0];
            const float height = row[3] - row[1];
            if (width >= 1.0F && height >= 1.0F) {
                decoded.push_back({class_id, score,
                                   {row[0], row[1], width, height}});
            }
        }
        return decoded;
    }

    if (shape[1] != class_count + 4 || shape[2] <= 0) {
        throw std::invalid_argument(
            "unsupported YOLO26 output; expected [1,300,6] or [1,4+nc,N]");
    }

    const std::size_t candidates = static_cast<std::size_t>(shape[2]);
    for (std::size_t index = 0; index < candidates; ++index) {
        int best_class = 0;
        float best_score = data[4 * candidates + index];
        for (int class_id = 1; class_id < class_count; ++class_id) {
            const float score = data[(4 + class_id) * candidates + index];
            if (score > best_score) {
                best_score = score;
                best_class = class_id;
            }
        }
        if (!std::isfinite(best_score) || best_score < confidence_threshold) {
            continue;
        }
        const float cx = data[index];
        const float cy = data[candidates + index];
        const float width = data[2 * candidates + index];
        const float height = data[3 * candidates + index];
        if (width >= 1.0F && height >= 1.0F) {
            decoded.push_back({best_class, best_score,
                               {cx - width * 0.5F, cy - height * 0.5F,
                                width, height}});
        }
    }
    return ClassAwareNms(std::move(decoded), iou_threshold);
}

} // namespace xunjing::detection
