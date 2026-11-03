#include "vision_control.h"
#include "dc_motor.h"
#include "serial_protocol.h"
#include "stepper_motor.h"

#define VISION_IMAGE_WIDTH_PX            1920.0f
#define VISION_IMAGE_HEIGHT_PX           1080.0f
#define VISION_TARGET_DISTANCE_M         0.8f
#define VISION_DISTANCE_KP_DEG_PER_M     450.0f
#define VISION_DISTANCE_KD_DEG_S_PER_M   27.0f
#define VISION_MAX_FORWARD_INCREMENT_DEG 50.0f
#define VISION_DISTANCE_DEADBAND_M       0.02f
#define VISION_TARGET_TIMEOUT_MS         150U

volatile VisionControlState_t g_vision_control;

static float vision_last_distance_error_m;
static float vision_filtered_base_target_deg;
static float vision_filtered_pitch_target_deg;
static uint32_t vision_last_tick_ms;
static uint8_t vision_has_sequence;

#define VISION_TARGET_FILTER_OLD_WEIGHT 0.90f
#define VISION_TARGET_FILTER_NEW_WEIGHT 0.10f

static float VisionControl_Clamp(float value, float minimum, float maximum)
{
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static float VisionControl_ApplyDeadband(float value, float deadband)
{
  return (value < deadband && value > -deadband) ? 0.0f : value;
}

void VisionControl_Init(void)
{
  g_vision_control.distance_error_m = 0.0f;
  g_vision_control.forward_angle_increment_deg = 0.0f;
  g_vision_control.last_sequence = 0U;
  g_vision_control.processed_frame_count = 0U;
  g_vision_control.target_active = 0U;
  vision_last_distance_error_m = 0.0f;
  vision_filtered_base_target_deg = g_gimbal_current_angle_deg.base_deg;
  vision_filtered_pitch_target_deg = g_gimbal_current_angle_deg.pitch_deg;
  vision_last_tick_ms = 0U;
  vision_has_sequence = 0U;
}

void VisionControl_Update(void)
{
  uint32_t now_ms;
  float dt_s;
  float distance_error_m;
  float distance_derivative;
  float forward_increment_deg;
  float x_px;
  float y_px;
  float base_target_deg;
  float pitch_target_deg;
  uint8_t distance_available;

  if (SerialProtocol_TargetIsFresh(VISION_TARGET_TIMEOUT_MS) == 0U)
  {
    if (g_vision_control.target_active != 0U)
    {
      LeftTrack_SetTargetAngle(g_left_track.current_angle_deg);
      RightTrack_SetTargetAngle(g_right_track.current_angle_deg);
      Stepper_SetTargetAngle(STEPPER_MOTOR_BASE,
                             g_gimbal_current_angle_deg.base_deg);
      Stepper_SetTargetAngle(STEPPER_MOTOR_PITCH,
                             g_gimbal_current_angle_deg.pitch_deg);
      vision_filtered_base_target_deg = g_gimbal_current_angle_deg.base_deg;
      vision_filtered_pitch_target_deg = g_gimbal_current_angle_deg.pitch_deg;
      g_vision_control.target_active = 0U;
    }
    return;
  }

  if ((vision_has_sequence != 0U) &&
      (g_serial_target.sequence == g_vision_control.last_sequence))
  {
    return;
  }

  now_ms = HAL_GetTick();
  dt_s = vision_has_sequence == 0U ? 0.033f :
      (float)(now_ms - vision_last_tick_ms) / 1000.0f;
  if (dt_s < 0.010f) dt_s = 0.010f;
  if (dt_s > 0.200f) dt_s = 0.200f;

  distance_available = g_serial_target.distance_m > 0.0f ? 1U : 0U;
  distance_error_m = distance_available != 0U ?
      g_serial_target.distance_m - VISION_TARGET_DISTANCE_M : 0.0f;

  x_px = (float)g_serial_target.center_x;
  if (x_px > VISION_IMAGE_WIDTH_PX - 1.0f)
    x_px = VISION_IMAGE_WIDTH_PX - 1.0f;
  y_px = (float)g_serial_target.center_y;
  if (y_px > VISION_IMAGE_HEIGHT_PX - 1.0f)
    y_px = VISION_IMAGE_HEIGHT_PX - 1.0f;

    base_target_deg = -40.0f + (80.0f * x_px / (VISION_IMAGE_WIDTH_PX - 1.0f));
    pitch_target_deg = 20.0f - (40.0f * y_px / (VISION_IMAGE_HEIGHT_PX - 1.0f));
    vision_filtered_base_target_deg =
      VISION_TARGET_FILTER_OLD_WEIGHT * vision_filtered_base_target_deg +
      VISION_TARGET_FILTER_NEW_WEIGHT * base_target_deg;
    vision_filtered_pitch_target_deg =
      VISION_TARGET_FILTER_OLD_WEIGHT * vision_filtered_pitch_target_deg +
      VISION_TARGET_FILTER_NEW_WEIGHT * pitch_target_deg;
    Stepper_SetTargetAngle(STEPPER_MOTOR_BASE, vision_filtered_base_target_deg);
    Stepper_SetTargetAngle(STEPPER_MOTOR_PITCH, vision_filtered_pitch_target_deg);

  distance_error_m = VisionControl_ApplyDeadband(distance_error_m,
                                                  VISION_DISTANCE_DEADBAND_M);

  distance_derivative = distance_available != 0U ?
      (distance_error_m - vision_last_distance_error_m) / dt_s : 0.0f;
  forward_increment_deg = VISION_DISTANCE_KP_DEG_PER_M * distance_error_m +
      VISION_DISTANCE_KD_DEG_S_PER_M * distance_derivative;
  forward_increment_deg = VisionControl_Clamp(forward_increment_deg,
      -VISION_MAX_FORWARD_INCREMENT_DEG, VISION_MAX_FORWARD_INCREMENT_DEG);

  if (distance_available != 0U)
  {
    LeftTrack_SetTargetAngle(g_left_track.target_angle_deg +
                             forward_increment_deg);
    RightTrack_SetTargetAngle(g_right_track.target_angle_deg +
                              forward_increment_deg);
  }

  g_vision_control.distance_error_m = distance_error_m;
  g_vision_control.forward_angle_increment_deg = forward_increment_deg;
  g_vision_control.last_sequence = g_serial_target.sequence;
  g_vision_control.processed_frame_count++;
  g_vision_control.target_active = 1U;
  vision_last_distance_error_m = distance_error_m;
  vision_last_tick_ms = now_ms;
  vision_has_sequence = 1U;
}
