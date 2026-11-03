#include "encoder_pwm.h"

volatile EncoderData_t g_encoder_data[ENCODER_COUNT];

static float encoder_last_raw_angle_deg[ENCODER_COUNT];
static float encoder_applied_zero_offset_deg[ENCODER_COUNT];
static uint8_t encoder_angle_initialized[ENCODER_COUNT];

static float Encoder_NormalizeSignedAngle(float angle_deg)
{
  while (angle_deg >= 180.0f) angle_deg -= 360.0f;
  while (angle_deg < -180.0f) angle_deg += 360.0f;
  return angle_deg;
}

static void Encoder_ResetData(uint32_t encoder_id)
{
  g_encoder_data[encoder_id].period_us = 0U;
  g_encoder_data[encoder_id].high_us = 0U;
  g_encoder_data[encoder_id].duty_percent = 0.0f;
  g_encoder_data[encoder_id].raw_angle_deg = 0.0f;
  g_encoder_data[encoder_id].zero_offset_deg =
      (encoder_id == ENCODER_MOTOR_PITCH) ?
          ENCODER_PITCH_ZERO_OFFSET_DEG : ENCODER_BASE_ZERO_OFFSET_DEG;
  g_encoder_data[encoder_id].angle_deg = 0.0f;
  g_encoder_data[encoder_id].valid = 0U;
  encoder_last_raw_angle_deg[encoder_id] = 0.0f;
  encoder_applied_zero_offset_deg[encoder_id] = 0.0f;
  encoder_angle_initialized[encoder_id] = 0U;
}

static void Encoder_ProcessCapture(uint32_t encoder_id, TIM_HandleTypeDef *htim)
{
  uint32_t period_count = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
  uint32_t high_count = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
  float measured_raw_angle_deg;
  float raw_angle_deg;
  float raw_delta_deg;

  if ((period_count == 0U) || (high_count > period_count))
  {
    g_encoder_data[encoder_id].valid = 0U;
    return;
  }

  g_encoder_data[encoder_id].period_us = period_count;
  g_encoder_data[encoder_id].high_us = high_count;
  g_encoder_data[encoder_id].duty_percent =
      ((float)high_count * 100.0f) / (float)period_count;

  measured_raw_angle_deg =
      ((float)high_count * 360.0f) / (float)period_count;
  if (measured_raw_angle_deg >= 360.0f)
  {
    measured_raw_angle_deg = 359.999f;
  }
  g_encoder_data[encoder_id].raw_angle_deg = measured_raw_angle_deg;
  raw_angle_deg = measured_raw_angle_deg;

  if ((raw_angle_deg <= ENCODER_RAW_ZERO_SNAP_DEG) ||
      (raw_angle_deg >= (360.0f - ENCODER_RAW_ZERO_SNAP_DEG)))
  {
    raw_angle_deg = 0.0f;
  }

  if (encoder_angle_initialized[encoder_id] == 0U)
  {
    g_encoder_data[encoder_id].angle_deg = Encoder_NormalizeSignedAngle(
        raw_angle_deg - g_encoder_data[encoder_id].zero_offset_deg);
    encoder_last_raw_angle_deg[encoder_id] = raw_angle_deg;
    encoder_applied_zero_offset_deg[encoder_id] =
        g_encoder_data[encoder_id].zero_offset_deg;
    encoder_angle_initialized[encoder_id] = 1U;
  }
  else
  {
    g_encoder_data[encoder_id].angle_deg -=
        g_encoder_data[encoder_id].zero_offset_deg -
        encoder_applied_zero_offset_deg[encoder_id];
    encoder_applied_zero_offset_deg[encoder_id] =
        g_encoder_data[encoder_id].zero_offset_deg;

    raw_delta_deg = raw_angle_deg - encoder_last_raw_angle_deg[encoder_id];
    if (raw_delta_deg > 180.0f) raw_delta_deg -= 360.0f;
    if (raw_delta_deg < -180.0f) raw_delta_deg += 360.0f;
    g_encoder_data[encoder_id].angle_deg += raw_delta_deg;
    encoder_last_raw_angle_deg[encoder_id] = raw_angle_deg;
  }

  g_encoder_data[encoder_id].valid = 1U;
}

void Encoder_PWM_Init(void)
{
  uint32_t encoder_id;
  for (encoder_id = 0U; encoder_id < ENCODER_COUNT; encoder_id++)
  {
    Encoder_ResetData(encoder_id);
  }

  if (HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
  if (HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_2) != HAL_OK) Error_Handler();
  if (HAL_TIM_IC_Start_IT(&htim5, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
  if (HAL_TIM_IC_Start_IT(&htim5, TIM_CHANNEL_2) != HAL_OK) Error_Handler();
}

float Encoder_PWM_GetAngle(uint32_t encoder_id)
{
  return encoder_id < ENCODER_COUNT ? g_encoder_data[encoder_id].angle_deg : 0.0f;
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  if ((htim == NULL) || (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1)) return;
  if (htim->Instance == TIM3) Encoder_ProcessCapture(ENCODER_MOTOR_PITCH, htim);
  if (htim->Instance == TIM5) Encoder_ProcessCapture(ENCODER_MOTOR_BASE, htim);
}
