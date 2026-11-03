#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xunjing::classification
{

struct ClassifierConfig
{
    bool enabled = false;
    std::function<std::vector<float>(const std::vector<float>&)> infer_logits;
    std::string model_path;
    std::vector<std::string> labels;
    int input_width = 224;
    int input_height = 224;
    int interval_frames = 5;
    int intra_op_threads = 2;
    float confidence_threshold = 0.45F;
    float smoothing_alpha = 0.35F;
    float roi_padding = 0.08F;
};

struct BehaviorResult
{
    int class_id = -1;
    std::string label;
    float confidence = 0.0F;
    bool fresh = false;
};

/**
 * PS-side ONNX Runtime classifier. Detection runs on the NPU, ByteTrack on PS;
 * the locked target ROI is classified every interval_frames and EMA-smoothed.
 */
class BehaviorClassifier
{
public:
    explicit BehaviorClassifier(ClassifierConfig config = {});
    ~BehaviorClassifier();

    BehaviorClassifier(const BehaviorClassifier&) = delete;
    BehaviorClassifier& operator=(const BehaviorClassifier&) = delete;
    BehaviorClassifier(BehaviorClassifier&&) noexcept;
    BehaviorClassifier& operator=(BehaviorClassifier&&) noexcept;

    bool Ready() const;
    const std::string& LastError() const;
    std::optional<BehaviorResult> Update(const cv::Mat& rgb565_frame,
                                         const cv::Rect2f& target_box,
                                         std::int32_t track_id);
    // Owned ROI already includes padding; retain full-frame coordinates for evidence.
    std::optional<BehaviorResult> UpdatePaddedRoi(const cv::Mat& rgb565_roi,
                                                 const cv::Rect& original_roi,
                                                 std::int32_t track_id);
    void Reset();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xunjing::classification
