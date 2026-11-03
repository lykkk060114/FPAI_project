#include <assert.h>
#include "../MDK-ARM/DC_Motor/dc_motor.c"

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim8;
TIM_HandleTypeDef htim9;
TIM_HandleTypeDef htim12;

HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
  (void)timer;
  (void)channel;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
  (void)timer;
  (void)channel;
  return HAL_OK;
}

int main(void)
{
  TIM_TypeDef right_encoder = {0};
  TIM_TypeDef left_encoder = {0};
  TIM_TypeDef left_pwm = {0};
  TIM_TypeDef right_pwm = {0};

  htim1.Instance = &right_encoder;
  htim8.Instance = &left_encoder;
  htim9.Instance = &left_pwm;
  htim12.Instance = &right_pwm;
  RightTrack_Init();
  LeftTrack_Init();

  right_encoder.CNT -= 1U;
  left_encoder.CNT += 1U;
  RightTrack_Update();
  LeftTrack_Update();
  assert(g_right_track.current_angle_deg > 0.0f);
  assert(g_left_track.current_angle_deg > 0.0f);

  RightTrack_SetDirection(1);
  RightTrack_SetDuty(20U);
  LeftTrack_SetDirection(1);
  LeftTrack_SetDuty(20U);
  assert(right_pwm.CCR1 == 180U && right_pwm.CCR2 == 200U);
  assert(left_pwm.CCR1 == 200U && left_pwm.CCR2 == 180U);

  RightTrack_SetDirection(-1);
  RightTrack_SetDuty(20U);
  LeftTrack_SetDirection(-1);
  LeftTrack_SetDuty(20U);
  assert(right_pwm.CCR1 == 200U && right_pwm.CCR2 == 180U);
  assert(left_pwm.CCR1 == 180U && left_pwm.CCR2 == 200U);
  return 0;
}