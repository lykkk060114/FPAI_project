#include "tracking/TargetTracker.hpp"

#include "BYTETracker.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace xunjing::tracking
{
namespace
{
cv::Rect2f ClampBox(const cv::Rect2f& box, int width, int height)
{
    const float left = std::clamp(box.x, 0.0F, static_cast<float>(width - 1));
    const float top = std::clamp(box.y, 0.0F, static_cast<float>(height - 1));
    const float right = std::clamp(box.x + box.width, left,
                                   static_cast<float>(width));
    const float bottom = std::clamp(box.y + box.height, top,
                                    static_cast<float>(height));
    return {left, top, right - left, bottom - top};
}

float Area(const cv::Rect2f& box)
{
    return std::max(0.0F, box.width) * std::max(0.0F, box.height);
}
} // namespace

class TargetTracker::Impl
{
public:
    explicit Impl(const TrackerConfig& config)
        : config_(config), tracker_(config.frame_rate, config.track_buffer)
    {
    }

    TrackerOutput Update(const std::vector<Detection>& detections)
    {
        std::vector<Object> objects;
        objects.reserve(detections.size());
        for (const auto& detection : detections) {
            if (!std::isfinite(detection.score) || detection.score < 0.0F ||
                !std::isfinite(detection.box.x) ||
                !std::isfinite(detection.box.y) ||
                !std::isfinite(detection.box.width) ||
                !std::isfinite(detection.box.height)) {
                continue;
            }
            const cv::Rect2f box = ClampBox(detection.box, config_.frame_width,
                                            config_.frame_height);
            if (box.width < 1.0F || box.height < 1.0F) {
                continue;
            }
            objects.push_back(Object{box, 0, detection.score});
        }

        const std::vector<STrack> active = tracker_.update(objects);
        TrackerOutput output;
        output.tracks.reserve(active.size());
        for (const auto& source : active) {
            if (source.tlwh.size() != 4) {
                continue;
            }
            const cv::Rect2f box = ClampBox(
                {source.tlwh[0], source.tlwh[1], source.tlwh[2], source.tlwh[3]},
                config_.frame_width, config_.frame_height);
            Track track;
            track.id = source.track_id;
            track.box = box;
            track.center = {box.x + box.width * 0.5F,
                            box.y + box.height * 0.5F};
            track.score = source.score;
            output.tracks.push_back(track);
        }

        auto locked_it = output.tracks.end();
        if (locked_track_id_) {
            locked_it = std::find_if(output.tracks.begin(), output.tracks.end(),
                                     [&](const Track& track) {
                                         return track.id == *locked_track_id_;
                                     });
            if (locked_it == output.tracks.end()) {
                ++missing_frames_;
                if (missing_frames_ > config_.track_buffer) {
                    locked_track_id_.reset();
                    missing_frames_ = 0;
                }
            } else {
                missing_frames_ = 0;
            }
        }

        if (!locked_track_id_ && !output.tracks.empty()) {
            locked_it = std::max_element(
                output.tracks.begin(), output.tracks.end(),
                [](const Track& left, const Track& right) {
                    return Area(left.box) < Area(right.box);
                });
            locked_track_id_ = locked_it->id;
            missing_frames_ = 0;
        }

        if (locked_track_id_) {
            locked_it = std::find_if(output.tracks.begin(), output.tracks.end(),
                                     [&](const Track& track) {
                                         return track.id == *locked_track_id_;
                                     });
            if (locked_it != output.tracks.end()) {
                locked_it->locked = true;
                output.locked_target = *locked_it;
            }
        }
        return output;
    }

    void ResetLock()
    {
        locked_track_id_.reset();
        missing_frames_ = 0;
    }

    TrackerConfig config_;
    BYTETracker tracker_;
    std::optional<std::int32_t> locked_track_id_;
    int missing_frames_ = 0;
};

TargetTracker::TargetTracker(const TrackerConfig& config)
    : impl_(new Impl(config))
{
}

TargetTracker::~TargetTracker()
{
    delete impl_;
}

TargetTracker::TargetTracker(TargetTracker&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr))
{
}

TargetTracker& TargetTracker::operator=(TargetTracker&& other) noexcept
{
    if (this != &other) {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

TrackerOutput TargetTracker::Update(const std::vector<Detection>& detections)
{
    return impl_->Update(detections);
}

void TargetTracker::ResetLock()
{
    impl_->ResetLock();
}

std::optional<std::int32_t> TargetTracker::LockedTrackId() const
{
    return impl_->locked_track_id_;
}

} // namespace xunjing::tracking
