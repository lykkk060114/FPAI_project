#pragma once

#include <opencv2/core/types.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace xunjing::tracking
{

struct Detection
{
    cv::Rect2f box{};
    float score = 0.0F;
};

struct Track
{
    std::int32_t id = -1;
    cv::Rect2f box{};
    cv::Point2f center{};
    float score = 0.0F;
    bool locked = false;
};

struct TrackerConfig
{
    int frame_rate = 30;
    int track_buffer = 15;
    int frame_width = 1920;
    int frame_height = 1080;
};

struct TrackerOutput
{
    std::vector<Track> tracks;
    std::optional<Track> locked_target;
};

/**
 * Thin project adapter around the official ByteTrack C++ implementation.
 *
 * The detector must filter to one target class before calling Update(). The
 * largest active track is locked first; that ID stays selected through short
 * detection gaps and is replaced only after track_buffer missing frames.
 */
class TargetTracker
{
public:
    explicit TargetTracker(const TrackerConfig& config = {});
    ~TargetTracker();

    TargetTracker(const TargetTracker&) = delete;
    TargetTracker& operator=(const TargetTracker&) = delete;
    TargetTracker(TargetTracker&&) noexcept;
    TargetTracker& operator=(TargetTracker&&) noexcept;

    TrackerOutput Update(const std::vector<Detection>& detections);
    void ResetLock();
    std::optional<std::int32_t> LockedTrackId() const;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace xunjing::tracking
