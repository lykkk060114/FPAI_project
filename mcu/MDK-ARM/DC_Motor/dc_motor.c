#include "dc_motor.h"

#define RIGHT_TRACK_PWM_TIMER       htim12
#define RIGHT_TRACK_PWM_IN1_CHANNEL TIM_CHANNEL_1
#define RIGHT_TRACK_PWM_IN2_CHANNEL TIM_CHANNEL_2
#define RIGHT_TRACK_MOTOR_DIRECTION_SIGN -1

#define RIGHT_TRACK_ENCODER_TIMER   htim1
#define RIGHT_TRACK_ENCODER_CENTER_COUNT 32768
#define RIGHT_TRACK_ENCODER_SIGN    -1.0f

#define RIGHT_TRACK_ENCODER_CPR     (13.0f * 30.0f * 4.0f)
#define RIGHT_TRACK_DEG_PER_COUNT   (360.0f / RIGHT_TRACK_ENCODER_CPR)

#define RIGHT_TRACK_CONTROL_PERIOD_S 0.001f
#define TRACK_SPEED_FILTER_ALPHA     0.10f
#define RIGHT_TRACK_KP               3.0f
#define RIGHT_TRACK_KD               0.06f
#define RIGHT_TRACK_DEFAULT_MAX_DUTY 180U
#define RIGHT_TRACK_MIN_DUTY         18U
#define RIGHT_TRACK_DUTY_RAMP_UP     2U
#define RIGHT_TRACK_DUTY_RAMP_DOWN   4U
#define RIGHT_TRACK_POSITION_DEADBAND_DEG 0.35f
#define RIGHT_TRACK_SPEED_DEADBAND_DEG_S  1.0f

volatile RightTrackState_t g_right_track = {0.0f, 0.0f, 0.0f, 0, RIGHT_TRACK_DEFAULT_MAX_DUTY, 0U};
volatile LeftTrackState_t g_left_track = {0.0f, 0.0f, 0.0f, 0, RIGHT_TRACK_DEFAULT_MAX_DUTY, 0U};
volatile uint8_t g_track_motion_enabled = 0U;

static uint16_t right_track_last_raw_count = RIGHT_TRACK_ENCODER_CENTER_COUNT;
static float right_track_filtered_speed_deg_s = 0.0f;
static uint16_t left_track_last_raw_count = 32768U;
static float left_track_filtered_speed_deg_s = 0.0f;
static int8_t right_track_active_direction = 1;
static int8_t left_track_active_direction = 1;

static int32_t Track_EncoderCountDelta(uint16_t current, uint16_t previous)
{
  int32_t delta = (int32_t)current - (int32_t)previous;

  if (delta > 32767)
  {
    delta -= 65536;
  }
  else if (delta < -32768)
  {
    delta += 65536;
  }

  return delta;
}

static float RightTrack_Clamp(float value, float minimum, float maximum)
{
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static void RightTrack_SetDuty(uint32_t duty)
{
  if (duty > 199U) duty = 199U;

  /* Slow decay: motor drive occurs while the PWM input is low. */
  if (right_track_active_direction >= 0)
  {
    __HAL_TIM_SET_COMPARE(&RIGHT_TRACK_PWM_TIMER,
                          RIGHT_TRACK_PWM_IN1_CHANNEL, 200U);
    __HAL_TIM_SET_COMPARE(&RIGHT_TRACK_PWM_TIMER,
                          RIGHT_TRACK_PWM_IN2_CHANNEL, 200U - duty);
  }
  else
  {
    __HAL_TIM_SET_COMPARE(&RIGHT_TRACK_PWM_TIMER,
                          RIGHT_TRACK_PWM_IN1_CHANNEL, 200U - duty);
    __HAL_TIM_SET_COMPARE(&RIGHT_TRACK_PWM_TIMER,
                          RIGHT_TRACK_PWM_IN2_CHANNEL, 200U);
  }
}

static void RightTrack_Brake(void)
{
  g_right_track.output_duty = 200U;
  __HAL_TIM_SET_COMPARE(&RIGHT_TRACK_PWM_TIMER,
                        RIGHT_TRACK_PWM_IN1_CHANNEL, 200U);
  __HAL_TIM_SET_COMPARE(&RIGHT_TRACK_PWM_TIMER,
                        RIGHT_TRACK_PWM_IN2_CHANNEL, 200U);
}

static void RightTrack_SetDirection(int8_t direction)
{
  int8_t output_direction = direction >= 0 ? 1 : -1;
  output_direction *= RIGHT_TRACK_MOTOR_DIRECTION_SIGN;
  if ((g_right_track.output_duty >= 200U) ||
      (output_direction != right_track_active_direction))
  {
    __HAL_TIM_SET_COMPARE(&RIGHT_TRACK_PWM_TIMER,
                          RIGHT_TRACK_PWM_IN1_CHANNEL, 200U);
    __HAL_TIM_SET_COMPARE(&RIGHT_TRACK_PWM_TIMER,
                          RIGHT_TRACK_PWM_IN2_CHANNEL, 200U);
    g_right_track.output_duty = 0U;
  }
  right_track_active_direction = output_direction;
}

void RightTrack_Init(void)
{
  __HAL_TIM_SET_COUNTER(&RIGHT_TRACK_ENCODER_TIMER,
                        RIGHT_TRACK_ENCODER_CENTER_COUNT);
  right_track_filtered_speed_deg_s = 0.0f;

  g_right_track.target_angle_deg = 0.0f;
  g_right_track.current_angle_deg = 0.0f;
  g_right_track.speed_deg_s = 0.0f;
  g_right_track.encoder_count = 0;
  g_right_track.max_duty = RIGHT_TRACK_DEFAULT_MAX_DUTY;
  g_right_track.output_duty = 0U;

  HAL_TIM_Encoder_Start(&RIGHT_TRACK_ENCODER_TIMER, TIM_CHANNEL_ALL);
  right_track_last_raw_count =
      (uint16_t)__HAL_TIM_GET_COUNTER(&RIGHT_TRACK_ENCODER_TIMER);
  g_right_track.encoder_count = 0;
  g_right_track.current_angle_deg = 0.0f;
  g_right_track.target_angle_deg = 0.0f;
  HAL_TIM_PWM_Start(&RIGHT_TRACK_PWM_TIMER, RIGHT_TRACK_PWM_IN1_CHANNEL);
  HAL_TIM_PWM_Start(&RIGHT_TRACK_PWM_TIMER, RIGHT_TRACK_PWM_IN2_CHANNEL);

  RightTrack_Brake();
}

void RightTrack_SetTargetAngle(float angle_deg)
{
  g_right_track.target_angle_deg = angle_deg;
}

void RightTrack_Update(void)
{
  int32_t count_delta;
  float error_deg;
  float control_duty;
    float raw_speed_deg_s;
  uint32_t desired_duty;
  uint32_t max_duty;
    uint16_t counter_value;

    counter_value = (uint16_t)__HAL_TIM_GET_COUNTER(&RIGHT_TRACK_ENCODER_TIMER);
    count_delta = Track_EncoderCountDelta(counter_value,
                       right_track_last_raw_count);
    right_track_last_raw_count = counter_value;

    g_right_track.encoder_count += count_delta;
  g_right_track.current_angle_deg =
      RIGHT_TRACK_ENCODER_SIGN * (float)g_right_track.encoder_count *
      RIGHT_TRACK_DEG_PER_COUNT;
    raw_speed_deg_s =
      RIGHT_TRACK_ENCODER_SIGN * (float)count_delta *
      RIGHT_TRACK_DEG_PER_COUNT /
      RIGHT_TRACK_CONTROL_PERIOD_S;
    right_track_filtered_speed_deg_s += TRACK_SPEED_FILTER_ALPHA *
      (raw_speed_deg_s - right_track_filtered_speed_deg_s);
    g_right_track.speed_deg_s = right_track_filtered_speed_deg_s;

  error_deg = g_right_track.target_angle_deg - g_right_track.current_angle_deg;

  if (g_track_motion_enabled == 0U)
  {
    RightTrack_Brake();
    return;
  }

  if ((error_deg < RIGHT_TRACK_POSITION_DEADBAND_DEG) &&
      (error_deg > -RIGHT_TRACK_POSITION_DEADBAND_DEG) &&
      (g_right_track.speed_deg_s < RIGHT_TRACK_SPEED_DEADBAND_DEG_S) &&
      (g_right_track.speed_deg_s > -RIGHT_TRACK_SPEED_DEADBAND_DEG_S))
  {
    RightTrack_Brake();
    return;
  }

  control_duty = RIGHT_TRACK_KP * error_deg -
                 RIGHT_TRACK_KD * g_right_track.speed_deg_s;
  if (control_duty >= 0.0f)
  {
    RightTrack_SetDirection(1);
  }
  else
  {
    RightTrack_SetDirection(-1);
    control_duty = -control_duty;
  }

  max_duty = g_right_track.max_duty;
  if (max_duty > 199U)
  {
    max_duty = 199U;
  }

  control_duty = RightTrack_Clamp(control_duty, 0.0f,
                                  (float)max_duty);
  desired_duty = (uint32_t)control_duty;

  if ((desired_duty > 0U) && (desired_duty < RIGHT_TRACK_MIN_DUTY))
  {
    desired_duty = RIGHT_TRACK_MIN_DUTY;
  }

  if (g_right_track.output_duty < desired_duty)
  {
    g_right_track.output_duty += RIGHT_TRACK_DUTY_RAMP_UP;
    if (g_right_track.output_duty > desired_duty)
    {
      g_right_track.output_duty = desired_duty;
    }
  }
  else if (g_right_track.output_duty > desired_duty)
  {
    if (g_right_track.output_duty > RIGHT_TRACK_DUTY_RAMP_DOWN)
    {
      g_right_track.output_duty -= RIGHT_TRACK_DUTY_RAMP_DOWN;
    }
    else
    {
      g_right_track.output_duty = 0U;
    }

    if (g_right_track.output_duty < desired_duty)
    {
      g_right_track.output_duty = desired_duty;
    }
  }

  RightTrack_SetDuty(g_right_track.output_duty);
}

#define LEFT_TRACK_PWM_TIMER       htim9
#define LEFT_TRACK_PWM_IN1_CHANNEL TIM_CHANNEL_1
#define LEFT_TRACK_PWM_IN2_CHANNEL TIM_CHANNEL_2
#define LEFT_TRACK_MOTOR_DIRECTION_SIGN 1
#define LEFT_TRACK_ENCODER_TIMER   htim8
#define LEFT_TRACK_ENCODER_SIGN    1.0f

static void LeftTrack_SetDuty(uint32_t duty)
{
  if (duty > 199U) duty = 199U;

  /* Slow decay: motor drive occurs while the PWM input is low. */
  if (left_track_active_direction >= 0)
  {
    __HAL_TIM_SET_COMPARE(&LEFT_TRACK_PWM_TIMER,
                          LEFT_TRACK_PWM_IN1_CHANNEL, 200U);
    __HAL_TIM_SET_COMPARE(&LEFT_TRACK_PWM_TIMER,
                          LEFT_TRACK_PWM_IN2_CHANNEL, 200U - duty);
  }
  else
  {
    __HAL_TIM_SET_COMPARE(&LEFT_TRACK_PWM_TIMER,
                          LEFT_TRACK_PWM_IN1_CHANNEL, 200U - duty);
    __HAL_TIM_SET_COMPARE(&LEFT_TRACK_PWM_TIMER,
                          LEFT_TRACK_PWM_IN2_CHANNEL, 200U);
  }
}

static void LeftTrack_Brake(void)
{
  g_left_track.output_duty = 200U;
  __HAL_TIM_SET_COMPARE(&LEFT_TRACK_PWM_TIMER,
                        LEFT_TRACK_PWM_IN1_CHANNEL, 200U);
  __HAL_TIM_SET_COMPARE(&LEFT_TRACK_PWM_TIMER,
                        LEFT_TRACK_PWM_IN2_CHANNEL, 200U);
}

static void LeftTrack_SetDirection(int8_t direction)
{
  int8_t output_direction;

  direction = direction >= 0 ? 1 : -1;
  output_direction = direction * LEFT_TRACK_MOTOR_DIRECTION_SIGN;
  if ((g_left_track.output_duty >= 200U) ||
      (output_direction != left_track_active_direction))
  {
    __HAL_TIM_SET_COMPARE(&LEFT_TRACK_PWM_TIMER,
                          LEFT_TRACK_PWM_IN1_CHANNEL, 200U);
    __HAL_TIM_SET_COMPARE(&LEFT_TRACK_PWM_TIMER,
                          LEFT_TRACK_PWM_IN2_CHANNEL, 200U);
    g_left_track.output_duty = 0U;
  }
  left_track_active_direction = output_direction;
}

void LeftTrack_Init(void)
{
  left_track_filtered_speed_deg_s = 0.0f;

  g_left_track.target_angle_deg = 0.0f;
  g_left_track.current_angle_deg = 0.0f;
  g_left_track.speed_deg_s = 0.0f;
  g_left_track.encoder_count = 0;
  g_left_track.max_duty = RIGHT_TRACK_DEFAULT_MAX_DUTY;
  g_left_track.output_duty = 0U;

  HAL_TIM_Encoder_Start(&LEFT_TRACK_ENCODER_TIMER, TIM_CHANNEL_ALL);
  left_track_last_raw_count =
      (uint16_t)__HAL_TIM_GET_COUNTER(&LEFT_TRACK_ENCODER_TIMER);
  g_left_track.encoder_count = 0;
  g_left_track.current_angle_deg = 0.0f;
  g_left_track.target_angle_deg = 0.0f;
  HAL_TIM_PWM_Start(&LEFT_TRACK_PWM_TIMER, LEFT_TRACK_PWM_IN1_CHANNEL);
  HAL_TIM_PWM_Start(&LEFT_TRACK_PWM_TIMER, LEFT_TRACK_PWM_IN2_CHANNEL);
  LeftTrack_Brake();
}

void LeftTrack_SetTargetAngle(float angle_deg)
{
  g_left_track.target_angle_deg = angle_deg;
}

void LeftTrack_Update(void)
{
    uint16_t counter_value =
      (uint16_t)__HAL_TIM_GET_COUNTER(&LEFT_TRACK_ENCODER_TIMER);
    int32_t count_delta = Track_EncoderCountDelta(counter_value,
                           left_track_last_raw_count);
  float error_deg;
  float control_duty;
    float raw_speed_deg_s;
  uint32_t desired_duty;
  uint32_t max_duty = g_left_track.max_duty;

    left_track_last_raw_count = counter_value;
    g_left_track.encoder_count += count_delta;
  g_left_track.current_angle_deg =
      LEFT_TRACK_ENCODER_SIGN * (float)g_left_track.encoder_count *
      RIGHT_TRACK_DEG_PER_COUNT;
    raw_speed_deg_s =
      LEFT_TRACK_ENCODER_SIGN * (float)count_delta *
      RIGHT_TRACK_DEG_PER_COUNT /
      RIGHT_TRACK_CONTROL_PERIOD_S;
    left_track_filtered_speed_deg_s += TRACK_SPEED_FILTER_ALPHA *
      (raw_speed_deg_s - left_track_filtered_speed_deg_s);
    g_left_track.speed_deg_s = left_track_filtered_speed_deg_s;

  error_deg = g_left_track.target_angle_deg - g_left_track.current_angle_deg;

  if (g_track_motion_enabled == 0U)
  {
    LeftTrack_Brake();
    return;
  }

  if ((error_deg < RIGHT_TRACK_POSITION_DEADBAND_DEG) &&
      (error_deg > -RIGHT_TRACK_POSITION_DEADBAND_DEG) &&
      (g_left_track.speed_deg_s < RIGHT_TRACK_SPEED_DEADBAND_DEG_S) &&
      (g_left_track.speed_deg_s > -RIGHT_TRACK_SPEED_DEADBAND_DEG_S))
  {
    LeftTrack_Brake();
    return;
  }

  control_duty = RIGHT_TRACK_KP * error_deg -
                 RIGHT_TRACK_KD * g_left_track.speed_deg_s;
  if (control_duty >= 0.0f) LeftTrack_SetDirection(1);
  else
  {
    LeftTrack_SetDirection(-1);
    control_duty = -control_duty;
  }

  if (max_duty > 199U) max_duty = 199U;
  control_duty = RightTrack_Clamp(control_duty, 0.0f, (float)max_duty);
  desired_duty = (uint32_t)control_duty;
  if ((desired_duty > 0U) && (desired_duty < RIGHT_TRACK_MIN_DUTY))
    desired_duty = RIGHT_TRACK_MIN_DUTY;

  if (g_left_track.output_duty < desired_duty)
  {
    g_left_track.output_duty += RIGHT_TRACK_DUTY_RAMP_UP;
    if (g_left_track.output_duty > desired_duty)
      g_left_track.output_duty = desired_duty;
  }
  else if (g_left_track.output_duty > desired_duty)
  {
    if (g_left_track.output_duty > RIGHT_TRACK_DUTY_RAMP_DOWN)
      g_left_track.output_duty -= RIGHT_TRACK_DUTY_RAMP_DOWN;
    else
      g_left_track.output_duty = 0U;
    if (g_left_track.output_duty < desired_duty)
      g_left_track.output_duty = desired_duty;
  }

  LeftTrack_SetDuty(g_left_track.output_duty);
}
