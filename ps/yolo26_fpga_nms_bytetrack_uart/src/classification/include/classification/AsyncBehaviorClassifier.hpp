#pragma once
#include "BehaviorClassifier.hpp"
#include "ClassifierPreprocess.hpp"
#include "../../../app/StageTiming.hpp"
#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace xunjing::classification {
// One running task + one replaceable pending task. Owns every submitted image.
// Update/Reset are called by the postprocessing thread; only the worker uses ORT.
class AsyncBehaviorClassifier {
    using Clock = std::chrono::steady_clock;
    struct Job { cv::Mat frame; cv::Rect roi; int id; unsigned long generation; Clock::time_point captured; };
    BehaviorClassifier classifier_;
    int interval_;
    float padding_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_ = false;
    unsigned long generation_ = 0, revision_ = 0, consumed_ = 0;
    int target_ = -1, frames_ = 0;
    std::optional<Job> pending_;
    std::optional<BehaviorResult> result_;
    Clock::time_point result_source_{};
    std::thread worker_;
    void Run() {
        unsigned long worker_generation = ~0UL;
        for (;;) {
            std::optional<Job> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [&]{ return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                job = std::move(pending_); pending_.reset();
            }
            if (worker_generation != job->generation) {
                classifier_.Reset(); worker_generation = job->generation;
            }
            xunjing::StageTimer job_timer("classifier_job", "async",job->id);
            auto result = classifier_.UpdatePaddedRoi(job->frame, job->roi, job->id);
            if(result)job_timer.Success();
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stopping_ && job->generation == generation_ && job->id == target_) {
                result_ = std::move(result); result_source_ = job->captured; ++revision_;
            }
        }
    }
public:
    explicit AsyncBehaviorClassifier(ClassifierConfig config)
        : classifier_([&]{ auto c = config; c.interval_frames = 1; return c; }()),
          interval_(std::max(1, config.interval_frames)), padding_(std::max(0.0F, config.roi_padding)) {
        if (classifier_.Ready()) worker_ = std::thread([this]{ Run(); });
    }
    ~AsyncBehaviorClassifier() { Stop(); }
    void Stop() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; pending_.reset(); }
        changed_.notify_one(); if (worker_.joinable()) worker_.join();
    }
    bool Ready() const { return classifier_.Ready(); }
    // Initialization error only; do not read the worker's mutable LastError.
    std::string InitialError() const { return Ready() ? std::string{} : classifier_.LastError(); }
    void Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (target_ != -1) { ++generation_; target_ = -1; frames_ = 0; pending_.reset(); result_.reset(); }
    }
    std::optional<BehaviorResult> Update(const cv::Mat& frame, const cv::Rect2f& box, int id) {
        if (!Ready() || frame.empty() || frame.type() != CV_8UC2 || id < 0) return {};
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopping_) return {};
        if (id != target_) { ++generation_; target_ = id; frames_ = 0; pending_.reset(); result_.reset(); }
        const auto now = Clock::now();
        if (frames_++ % interval_ == 0) {
            if (!std::isfinite(box.x) || !std::isfinite(box.y) ||
                !std::isfinite(box.width) || !std::isfinite(box.height) ||
                box.width <= 0 || box.height <= 0 || box.x >= frame.cols ||
                box.y >= frame.rows || box.x + box.width <= 0 ||
                box.y + box.height <= 0) return {};
            const auto generation = generation_;
            const auto roi = preprocess_detail::PaddedRoi(box, frame.size(), padding_);
            lock.unlock();
            cv::Mat owned;
            { xunjing::StageTimer timer("classifier_roi_copy", "cpu", id);
              owned = frame(roi).clone(); timer.Success(); }
            lock.lock();
            if (stopping_ || generation != generation_ || id != target_) return {};
            pending_ = Job{std::move(owned), roi, id, generation, now};
            changed_.notify_one();
        }
        if (!result_ || now - result_source_ > std::chrono::seconds(2)) return {};
        auto result = *result_; result.fresh = revision_ != consumed_; consumed_ = revision_;
        return result;
    }
};
}
