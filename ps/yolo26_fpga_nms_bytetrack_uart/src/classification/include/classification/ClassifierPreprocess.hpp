#pragma once
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#include "BehaviorClassifier.hpp"
namespace xunjing::classification {
struct PreparedClassifierInput {
    cv::Rect roi;
    cv::Mat rgb;
    std::vector<float> input;
};
namespace preprocess_detail {
inline cv::Rect PaddedRoi(const cv::Rect2f& box, const cv::Size& frame_size,
                   float padding)
{
    const float dx = box.width * padding;
    const float dy = box.height * padding;
    const int left = std::clamp(static_cast<int>(std::floor(box.x - dx)), 0,
                                frame_size.width - 1);
    const int top = std::clamp(static_cast<int>(std::floor(box.y - dy)), 0,
                               frame_size.height - 1);
    const int right = std::clamp(
        static_cast<int>(std::ceil(box.x + box.width + dx)), left + 1,
        frame_size.width);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil(box.y + box.height + dy)), top + 1,
        frame_size.height);
    return {left, top, right - left, bottom - top};
}

}
inline std::optional<PreparedClassifierInput> PrepareClassifierInput(
    const cv::Mat& frame, const cv::Rect2f& target_box, const ClassifierConfig& config_) {
    if (frame.empty() || frame.type()!=CV_8UC2 || config_.input_width<=0 ||
        config_.input_height<=0 || !std::isfinite(config_.roi_padding) || config_.roi_padding<0) return {};
        if (!std::isfinite(target_box.x) || !std::isfinite(target_box.y) ||
            !std::isfinite(target_box.width) || !std::isfinite(target_box.height) ||
            target_box.width <= 0 || target_box.height <= 0 ||
            target_box.x >= frame.cols || target_box.y >= frame.rows ||
            target_box.x + target_box.width <= 0 || target_box.y + target_box.height <= 0)
            return {};
    using preprocess_detail::PaddedRoi;
            const cv::Rect roi = PaddedRoi(target_box, frame.size(),
                                           config_.roi_padding);
            cv::Mat rgb;
            cv::cvtColor(frame(roi), rgb, cv::COLOR_BGR5652RGB);
            // Match the aspect-preserving 224x224 dataset crops; retain water/food context.
            const double scale = std::min(static_cast<double>(config_.input_width) / rgb.cols,
                                          static_cast<double>(config_.input_height) / rgb.rows);
            const int width = std::clamp(static_cast<int>(std::round(rgb.cols * scale)), 1, config_.input_width);
            const int height = std::clamp(static_cast<int>(std::round(rgb.rows * scale)), 1, config_.input_height);
            cv::resize(rgb, rgb, {width, height}, 0.0, 0.0, cv::INTER_LINEAR);
            const int left = (config_.input_width - width) / 2;
            const int top = (config_.input_height - height) / 2;
            cv::copyMakeBorder(rgb, rgb, top, config_.input_height - height - top,
                              left, config_.input_width - width - left,
                              cv::BORDER_CONSTANT, cv::Scalar(127, 127, 127));

            constexpr float mean[3] = {0.485F, 0.456F, 0.406F};
            constexpr float stddev[3] = {0.229F, 0.224F, 0.225F};
            const std::size_t plane = static_cast<std::size_t>(config_.input_width) *
                static_cast<std::size_t>(config_.input_height);
            std::vector<float> input(plane * 3);
            for (int y = 0; y < rgb.rows; ++y) {
                const auto* row = rgb.ptr<cv::Vec3b>(y);
                for (int x = 0; x < rgb.cols; ++x) {
                    const std::size_t pixel = static_cast<std::size_t>(y) * rgb.cols + x;
                    for (int channel = 0; channel < 3; ++channel) {
                        const float value = row[x][channel] / 255.0F;
                        input[channel * plane + pixel] =
                            (value - mean[channel]) / stddev[channel];
                    }
                }
            }

    return PreparedClassifierInput{roi, rgb, std::move(input)};
}
} // namespace xunjing::classification
