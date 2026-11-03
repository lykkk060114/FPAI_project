#pragma once

#include <opencv2/core/types.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xunjing::detection
{

struct Detection
{
    int class_id = -1;
    float confidence = 0.0F;
    cv::Rect2f box{};
};

struct Yolo26OutputView
{
    const float *data = nullptr;
    std::vector<int64_t> shape;
};

/** Decode the six one-to-one Conv outputs emitted by the converted model.
 * The order is cls8, box8, cls16, box16, cls32, box32; tensors are NHWC.
 * CPU class-aware NMS runs before the final result limit and tracking.
 */
std::vector<Detection> DecodeYolo26Conv(
    const std::vector<Yolo26OutputView> &outputs,
    int class_count,
    float confidence_threshold,
    float iou_threshold = 0.6F,
    std::size_t max_detections = 300,
    std::size_t* candidates_before_nms = nullptr);

/**
 * Decode either supported Ultralytics YOLO26 detection export:
 *   end-to-end: [1, 300, 6], rows are x1,y1,x2,y2,confidence,class_id;
 *   one-to-many: [1, 4+nc, anchors], channels are cx,cy,w,h,class scores.
 * End-to-end output is already duplicate-free. NMS is applied only to the
 * one-to-many output.
 */
std::vector<Detection> DecodeYolo26(const float* data,
                                    const std::vector<int64_t>& shape,
                                    int class_count,
                                    float confidence_threshold,
                                    float iou_threshold);

} // namespace xunjing::detection
