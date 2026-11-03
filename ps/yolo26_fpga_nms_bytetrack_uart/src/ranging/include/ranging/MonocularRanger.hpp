#pragma once

#include <cstdint>
#include <optional>

namespace xunjing::ranging
{

struct RangerConfig
{
    bool enabled = false;
    // distance_m = scale_m_px / bbox_height_px + bias_m
    // scale_m_px is normally focal_length_px * representative_target_height_m.
    float scale_m_px = 0.0F;
    float bias_m = 0.0F;
    float min_distance_m = 0.2F;
    float max_distance_m = 100.0F;
    float smoothing_alpha = 0.35F;
};

class MonocularRanger
{
public:
    explicit MonocularRanger(RangerConfig config = {});

    std::optional<float> Estimate(std::int32_t track_id,
                                  float bbox_height_px);
    void Reset();
    bool Calibrated() const;

private:
    RangerConfig config_;
    std::optional<std::int32_t> last_track_id_;
    std::optional<float> smoothed_distance_m_;
};

} // namespace xunjing::ranging
