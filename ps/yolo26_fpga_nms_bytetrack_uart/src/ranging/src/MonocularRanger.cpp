#include "ranging/MonocularRanger.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace xunjing::ranging
{

MonocularRanger::MonocularRanger(RangerConfig config)
    : config_(std::move(config))
{
    if (!std::isfinite(config_.min_distance_m) || config_.min_distance_m < 0.0F)
        config_.min_distance_m = 0.2F;
    if (!std::isfinite(config_.max_distance_m) ||
        config_.max_distance_m < config_.min_distance_m)
        config_.max_distance_m = 30.0F;
    config_.smoothing_alpha = std::clamp(config_.smoothing_alpha, 0.0F, 1.0F);
}

std::optional<float> MonocularRanger::Estimate(std::int32_t track_id,
                                                float bbox_height_px)
{
    if (!Calibrated() || track_id < 0 || !std::isfinite(bbox_height_px) ||
        bbox_height_px < 1.0F) {
        return std::nullopt;
    }

    float distance_m = config_.scale_m_px / bbox_height_px + config_.bias_m;
    if (!std::isfinite(distance_m) || distance_m <= 0.0F) {
        return std::nullopt;
    }
    distance_m = std::clamp(distance_m, config_.min_distance_m,
                            config_.max_distance_m);

    if (!last_track_id_ || *last_track_id_ != track_id) {
        last_track_id_ = track_id;
        smoothed_distance_m_ = distance_m;
    } else if (smoothed_distance_m_) {
        smoothed_distance_m_ = config_.smoothing_alpha * distance_m +
            (1.0F - config_.smoothing_alpha) * *smoothed_distance_m_;
    } else {
        smoothed_distance_m_ = distance_m;
    }
    return smoothed_distance_m_;
}

void MonocularRanger::Reset()
{
    last_track_id_.reset();
    smoothed_distance_m_.reset();
}

bool MonocularRanger::Calibrated() const
{
    return config_.enabled && std::isfinite(config_.scale_m_px) &&
        config_.scale_m_px > 0.0F;
}

} // namespace xunjing::ranging
