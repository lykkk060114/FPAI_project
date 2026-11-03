#include <assert.h>
#include "../MDK-ARM/Vision_Control/vision_control.c"
#include "../MDK-ARM/Stepper__Motor/stepper_motor.h"

static uint32_t test_tick;
volatile RightTrackState_t g_right_track;
volatile LeftTrackState_t g_left_track;
volatile SerialTarget_t g_serial_target;
volatile GimbalAngleCommand_t g_gimbal_target_angle_deg;
volatile GimbalAngleCommand_t g_gimbal_current_angle_deg;

uint32_t HAL_GetTick(void) { return test_tick; }
uint8_t SerialProtocol_TargetIsFresh(uint32_t timeout)
{
  (void)timeout;
  return g_serial_target.valid;
}
void LeftTrack_SetTargetAngle(float value) { g_left_track.target_angle_deg = value; }
void RightTrack_SetTargetAngle(float value) { g_right_track.target_angle_deg = value; }
void Stepper_SetTargetAngle(uint32_t motor_id, float angle_deg)
{
  if (motor_id == STEPPER_MOTOR_BASE) g_gimbal_target_angle_deg.base_deg = angle_deg;
  else g_gimbal_target_angle_deg.pitch_deg = angle_deg;
}

int main(void)
{
  VisionControl_Init();
  test_tick = 100U;
  g_serial_target.valid = 1U;
  g_serial_target.sequence = 1U;
  g_serial_target.distance_m = 2.0f;
  g_serial_target.center_x = 960U;
  g_serial_target.center_y = 540U;
  VisionControl_Update();
  assert(g_left_track.target_angle_deg > 0.0f);
  assert(g_right_track.target_angle_deg == g_left_track.target_angle_deg);
    assert(g_gimbal_target_angle_deg.base_deg > -0.003f &&
      g_gimbal_target_angle_deg.base_deg < 0.003f);
    assert(g_gimbal_target_angle_deg.pitch_deg > -0.003f &&
      g_gimbal_target_angle_deg.pitch_deg < 0.003f);

      g_left_track.target_angle_deg = 0.0f;
      g_right_track.target_angle_deg = 0.0f;
  test_tick += 33U;
  g_serial_target.sequence = 2U;
  g_serial_target.distance_m = 0.5f;
  g_serial_target.center_x = 0U;
  g_serial_target.center_y = 0U;
  VisionControl_Update();
  assert(g_left_track.target_angle_deg < 0.0f);
  assert(g_right_track.target_angle_deg == g_left_track.target_angle_deg);
      assert(g_gimbal_target_angle_deg.base_deg > -4.01f &&
        g_gimbal_target_angle_deg.base_deg < -3.99f);
      assert(g_gimbal_target_angle_deg.pitch_deg > 1.99f &&
        g_gimbal_target_angle_deg.pitch_deg < 2.01f);

  test_tick += 33U;
  g_serial_target.sequence = 3U;
  g_serial_target.distance_m = 0.0f;
  g_serial_target.center_x = 1919U;
  g_serial_target.center_y = 1079U;
  {
    float left_target_before = g_left_track.target_angle_deg;
    float right_target_before = g_right_track.target_angle_deg;
    VisionControl_Update();
    assert(g_left_track.target_angle_deg == left_target_before);
    assert(g_right_track.target_angle_deg == right_target_before);
            assert(g_gimbal_target_angle_deg.base_deg > 0.39f &&
              g_gimbal_target_angle_deg.base_deg < 0.41f);
            assert(g_gimbal_target_angle_deg.pitch_deg > -0.21f &&
              g_gimbal_target_angle_deg.pitch_deg < -0.19f);
  }
  return 0;
}