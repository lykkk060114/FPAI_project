#include "classification/BehaviorClassifier.hpp"
#include "classification/ClassifierPreprocess.hpp"
#include "../../app/FrameReferenceCapture.hpp"
#include "../../app/StageTiming.hpp"

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace xunjing::classification
{
namespace
{
std::vector<float> Softmax(const float* logits, std::size_t count)
{
    const float maximum = *std::max_element(logits, logits + count);
    std::vector<float> probabilities(count);
    float sum = 0.0F;
    for (std::size_t i = 0; i < count; ++i) {
        probabilities[i] = std::exp(logits[i] - maximum);
        sum += probabilities[i];
    }
    if (sum > 0.0F) {
        for (float& value : probabilities) {
            value /= sum;
        }
    }
    return probabilities;
}

} // namespace

class BehaviorClassifier::Impl
{
public:
    explicit Impl(ClassifierConfig config)
        : config_(std::move(config)), env_(ORT_LOGGING_LEVEL_WARNING, "xunjing")
    {
        config_.interval_frames = std::max(1, config_.interval_frames);
        config_.smoothing_alpha = std::clamp(config_.smoothing_alpha, 0.0F, 1.0F);
        config_.roi_padding = std::max(0.0F, config_.roi_padding);
        if (!config_.enabled) {
            last_error_ = "classifier disabled";
            return;
        }
        if (config_.labels.empty()) {
            last_error_ = "classifier labels are empty";
            return;
        }
        if (config_.input_width <= 0 || config_.input_height <= 0 ||
            !std::isfinite(config_.confidence_threshold) ||
            config_.confidence_threshold < 0.0F || config_.confidence_threshold > 1.0F ||
            !std::isfinite(config_.smoothing_alpha) || !std::isfinite(config_.roi_padding)) {
            last_error_ = "invalid classifier configuration";
            return;
        }
        if (config_.infer_logits) { ready_ = true; return; }
        if (!std::filesystem::exists(config_.model_path)) {
            last_error_ = "classifier model does not exist: " + config_.model_path;
            return;
        }
        try {
            Ort::SessionOptions options;
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            options.SetIntraOpNumThreads(std::max(1, config_.intra_op_threads));
            session_ = std::make_unique<Ort::Session>(env_, config_.model_path.c_str(),
                                                      options);
            if (session_->GetInputCount() != 1 || session_->GetOutputCount() != 1)
                throw std::runtime_error("classifier requires one input and one output");
            auto input_type = session_->GetInputTypeInfo(0);
            auto input_info = input_type.GetTensorTypeAndShapeInfo();
            const std::vector<int64_t> expected{1, 3, config_.input_height, config_.input_width};
            if (input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                input_info.GetShape() != expected)
                throw std::runtime_error("classifier input must be float32 [1,3,H,W] matching YAML");
            auto output_type = session_->GetOutputTypeInfo(0);
            auto output_info = output_type.GetTensorTypeAndShapeInfo();
            if (output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                output_info.GetShape() != std::vector<int64_t>{1, static_cast<int64_t>(config_.labels.size())})
                throw std::runtime_error("classifier output must be float32 [1,number_of_labels] logits");
            Ort::AllocatorWithDefaultOptions allocator;
            auto input_name = session_->GetInputNameAllocated(0, allocator);
            auto output_name = session_->GetOutputNameAllocated(0, allocator);
            input_name_ = input_name.get();
            output_name_ = output_name.get();
            ready_ = true;
            last_error_.clear();
        } catch (const std::exception& exception) {
            last_error_ = exception.what();
        }
    }

    std::optional<BehaviorResult> Update(const cv::Mat& frame,
                                         const cv::Rect2f& target_box,
                                         std::int32_t track_id,
                                         std::optional<cv::Rect> copied_roi = {})
    {
        if (!ready_ || frame.empty() || frame.type() != CV_8UC2 || track_id < 0) {
            return std::nullopt;
        }
        if (!std::isfinite(target_box.x) || !std::isfinite(target_box.y) ||
            !std::isfinite(target_box.width) || !std::isfinite(target_box.height) ||
            target_box.width <= 0 || target_box.height <= 0 ||
            target_box.x >= frame.cols || target_box.y >= frame.rows ||
            target_box.x + target_box.width <= 0 || target_box.y + target_box.height <= 0)
            return std::nullopt;
        if (!last_track_id_ || *last_track_id_ != track_id) {
            Reset();
            last_track_id_ = track_id;
        }
        ++frame_counter_;
        const bool should_run = !last_result_ ||
            frame_counter_ % config_.interval_frames == 0;
        if (!should_run) {
            BehaviorResult cached = *last_result_;
            cached.fresh = false;
            return cached;
        }

        try {
            xunjing::StageTimer total_timer("classifier_total",config_.infer_logits ? "zg330" : "cpu",track_id);
            auto prepared = [&]{xunjing::StageTimer timer("classifier_preprocess",config_.infer_logits ? "zg330" : "cpu",track_id);auto local_config=config_; if(copied_roi) local_config.roi_padding=0.0F; auto out=PrepareClassifierInput(frame,target_box,local_config); if(out && copied_roi) out->roi=*copied_roi;if(out)timer.Success();return out;}();
            if (!prepared) return std::nullopt;
            auto& input = prepared->input;
            xunjing::reference::FrameCapture::Classification(prepared->roi, prepared->rgb, input);

            std::vector<float> values;
            {
            xunjing::StageTimer infer_timer("classifier_infer",config_.infer_logits ? "zg330" : "cpu",track_id);
            if(config_.infer_logits) values = config_.infer_logits(input);
            else {
            const std::array<int64_t, 4> input_shape{
                1, 3, config_.input_height, config_.input_width};
            auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,
                                                     OrtMemTypeDefault);
            Ort::Value tensor = Ort::Value::CreateTensor<float>(
                memory, input.data(), input.size(), input_shape.data(),
                input_shape.size());
            const char* input_names[] = {input_name_.c_str()};
            const char* output_names[] = {output_name_.c_str()};
            auto outputs = session_->Run(Ort::RunOptions{nullptr}, input_names,
                                         &tensor, 1, output_names, 1);
            const auto info = outputs[0].GetTensorTypeAndShapeInfo();
            const std::size_t count = info.GetElementCount();
            if (count != config_.labels.size()) {
                last_error_ = "classifier output count does not match labels";
                return std::nullopt;
            }
            values.assign(outputs[0].GetTensorData<float>(), outputs[0].GetTensorData<float>() + count);
            }
            infer_timer.Success();
            }
            const auto count = values.size();
            if(count != config_.labels.size()) throw std::runtime_error("classifier output count mismatch");
            const float* logits = values.data();
            if (!std::all_of(logits, logits + count, [](float v) { return std::isfinite(v); })) {
                last_error_ = "classifier returned non-finite logits";
                return std::nullopt;
            }
            const std::vector<float> probabilities = Softmax(logits, count);
            if (smoothed_probabilities_.empty()) {
                smoothed_probabilities_ = probabilities;
            } else {
                for (std::size_t i = 0; i < count; ++i) {
                    smoothed_probabilities_[i] =
                        config_.smoothing_alpha * probabilities[i] +
                        (1.0F - config_.smoothing_alpha) *
                            smoothed_probabilities_[i];
                }
            }
            const auto best = std::max_element(smoothed_probabilities_.begin(),
                                               smoothed_probabilities_.end());
            const int class_id = static_cast<int>(
                std::distance(smoothed_probabilities_.begin(), best));
            BehaviorResult result{class_id, config_.labels[class_id], *best, true};
            if (result.confidence < config_.confidence_threshold) {
                result.class_id = -1;
                result.label = "uncertain";
            }
            last_error_.clear();
            last_result_ = result;
            total_timer.Success();
            return result;
        } catch (const std::exception& exception) {
            last_error_ = exception.what();
            return std::nullopt;
        }
    }

    void Reset()
    {
        frame_counter_ = 0;
        last_track_id_.reset();
        smoothed_probabilities_.clear();
        last_result_.reset();
    }

    ClassifierConfig config_;
    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::string output_name_;
    std::string last_error_;
    bool ready_ = false;
    int frame_counter_ = 0;
    std::optional<std::int32_t> last_track_id_;
    std::vector<float> smoothed_probabilities_;
    std::optional<BehaviorResult> last_result_;
};

BehaviorClassifier::BehaviorClassifier(ClassifierConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

BehaviorClassifier::~BehaviorClassifier() = default;
BehaviorClassifier::BehaviorClassifier(BehaviorClassifier&&) noexcept = default;
BehaviorClassifier& BehaviorClassifier::operator=(BehaviorClassifier&&) noexcept = default;

bool BehaviorClassifier::Ready() const { return impl_->ready_; }
const std::string& BehaviorClassifier::LastError() const { return impl_->last_error_; }

std::optional<BehaviorResult> BehaviorClassifier::Update(
    const cv::Mat& frame, const cv::Rect2f& target_box, std::int32_t track_id)
{
    return impl_->Update(frame, target_box, track_id);
}

std::optional<BehaviorResult> BehaviorClassifier::UpdatePaddedRoi(
    const cv::Mat& frame, const cv::Rect& original_roi, std::int32_t track_id)
{
    if (frame.size() != original_roi.size()) return std::nullopt;
    return impl_->Update(frame, cv::Rect2f(0, 0, frame.cols, frame.rows), track_id, original_roi);
}

void BehaviorClassifier::Reset() { impl_->Reset(); }

} // namespace xunjing::classification
