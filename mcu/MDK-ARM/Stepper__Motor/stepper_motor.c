#include "stepper_motor.h"

volatile GimbalAngleCommand_t g_gimbal_target_angle_deg = {0.0f, 0.0f};
volatile GimbalAngleCommand_t g_gimbal_current_angle_deg = {0.0f, 0.0f};

typedef struct
{
  TIM_HandleTypeDef *pwm_timer;
  uint32_t pwm_channel;
  GPIO_TypeDef *en_port;
  uint16_t en_pin;
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin;
  GPIO_PinState positive_dir_level;
  volatile int32_t remaining_steps;
  volatile int8_t direction;
  volatile uint8_t in_position;
  float last_target_angle_deg;
} StepperMotorState_t;

static StepperMotorState_t g_stepper_motor[STEPPER_MOTOR_COUNT] =
{
  {&htim2, TIM_CHANNEL_1, GPIOB, GPIO_PIN_0, GPIOB, GPIO_PIN_1,
   STEPPER_PITCH_POSITIVE_DIR_LEVEL, 0, STEPPER_DIRECTION_CW, 0U, 0.0f},
  {&htim4, TIM_CHANNEL_1, GPIOC, GPIO_PIN_0, GPIOC, GPIO_PIN_1,
   STEPPER_BASE_POSITIVE_DIR_LEVEL, 0, STEPPER_DIRECTION_CW, 0U, 0.0f}
};

static float *Stepper_TargetAngle(uint32_t motor_id)
{
  return motor_id == STEPPER_MOTOR_BASE ?
      (float *)&g_gimbal_target_angle_deg.base_deg :
      (float *)&g_gimbal_target_angle_deg.pitch_deg;
}

static float *Stepper_CurrentAngle(uint32_t motor_id)
{
  return motor_id == STEPPER_MOTOR_BASE ?
      (float *)&g_gimbal_current_angle_deg.base_deg :
      (float *)&g_gimbal_current_angle_deg.pitch_deg;
}

static float Stepper_ClampAngle(uint32_t motor_id, float angle_deg)
{
  float limit = motor_id == STEPPER_MOTOR_BASE ?
      STEPPER_BASE_LIMIT_DEG : STEPPER_PITCH_LIMIT_DEG;
  if (angle_deg > limit) return limit;
  if (angle_deg < -limit) return -limit;
  return angle_deg;
}

static void Stepper_StartBatch(uint32_t motor_id, uint32_t steps)
{
  StepperMotorState_t *motor = &g_stepper_motor[motor_id];
  motor->remaining_steps = (int32_t)steps;
  Stepper_SetEnable(motor_id, 1U);
  __HAL_TIM_SET_COUNTER(motor->pwm_timer, 0U);
  HAL_TIM_PWM_Start_IT(motor->pwm_timer, motor->pwm_channel);
}

void Stepper_Init(void)
{
  uint32_t motor_id;
  for (motor_id = 0U; motor_id < STEPPER_MOTOR_COUNT; motor_id++)
  {
    g_stepper_motor[motor_id].remaining_steps = 0;
    g_stepper_motor[motor_id].direction = STEPPER_DIRECTION_CW;
    g_stepper_motor[motor_id].in_position = 0U;
    g_stepper_motor[motor_id].last_target_angle_deg = 0.0f;
    *Stepper_TargetAngle(motor_id) = 0.0f;
    *Stepper_CurrentAngle(motor_id) = 0.0f;
    Stepper_SetEnable(motor_id, 0U);
    Stepper_SetDirection(motor_id, STEPPER_DIRECTION_CW);
    Stepper_Stop(motor_id);
  }
}

void Stepper_SetEnable(uint32_t motor_id, uint8_t enable)
{
  if (motor_id >= STEPPER_MOTOR_COUNT) return;
  HAL_GPIO_WritePin(g_stepper_motor[motor_id].en_port,
                    g_stepper_motor[motor_id].en_pin,
                    enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Stepper_SetDirection(uint32_t motor_id, int8_t direction)
{
  GPIO_PinState level;
  if (motor_id >= STEPPER_MOTOR_COUNT) return;
  g_stepper_motor[motor_id].direction =
      direction >= 0 ? STEPPER_DIRECTION_CW : STEPPER_DIRECTION_CCW;
  level = g_stepper_motor[motor_id].direction >= 0 ?
      g_stepper_motor[motor_id].positive_dir_level :
      (g_stepper_motor[motor_id].positive_dir_level == GPIO_PIN_SET ?
          GPIO_PIN_RESET : GPIO_PIN_SET);
  HAL_GPIO_WritePin(g_stepper_motor[motor_id].dir_port,
                    g_stepper_motor[motor_id].dir_pin, level);
}

void Stepper_SetTargetAngle(uint32_t motor_id, float angle_deg)
{
  if (motor_id >= STEPPER_MOTOR_COUNT) return;
  *Stepper_TargetAngle(motor_id) = Stepper_ClampAngle(motor_id, angle_deg);
  g_stepper_motor[motor_id].in_position = 0U;
}

void Stepper_SetCurrentAngle(uint32_t motor_id, float angle_deg)
{
  if (motor_id < STEPPER_MOTOR_COUNT) *Stepper_CurrentAngle(motor_id) = angle_deg;
}

void Stepper_Stop(uint32_t motor_id)
{
  if (motor_id >= STEPPER_MOTOR_COUNT) return;
  HAL_TIM_PWM_Stop_IT(g_stepper_motor[motor_id].pwm_timer,
                     g_stepper_motor[motor_id].pwm_channel);
  g_stepper_motor[motor_id].remaining_steps = 0;
}

void Stepper_UpdateControl(void)
{
  uint32_t motor_id;
  for (motor_id = 0U; motor_id < STEPPER_MOTOR_COUNT; motor_id++)
  {
    StepperMotorState_t *motor = &g_stepper_motor[motor_id];
    float target_angle = Stepper_ClampAngle(
      motor_id, *Stepper_TargetAngle(motor_id));
    float current_angle = *Stepper_CurrentAngle(motor_id);
    float delta_angle = target_angle - current_angle;
    float target_change = target_angle - motor->last_target_angle_deg;
    int32_t target_steps;

    *Stepper_TargetAngle(motor_id) = target_angle;
    if ((target_change > 0.001f) || (target_change < -0.001f))
    {
      motor->in_position = 0U;
      motor->last_target_angle_deg = target_angle;
    }
    if (motor->in_position != 0U)
    {
      if ((delta_angle < STEPPER_RESTART_ERROR_DEG) &&
          (delta_angle > -STEPPER_RESTART_ERROR_DEG)) continue;
      motor->in_position = 0U;
    }

    if (delta_angle > STEPPER_POSITION_DEADBAND_DEG)
    {
        if (current_angle >=
          ((motor_id == STEPPER_MOTOR_BASE ? STEPPER_BASE_LIMIT_DEG :
                           STEPPER_PITCH_LIMIT_DEG) - STEPPER_ANGLE_PER_PULSE))
      {
        Stepper_Stop(motor_id);
        motor->in_position = 1U;
        continue;
      }
      target_steps = (int32_t)(delta_angle / STEPPER_ANGLE_PER_PULSE + 0.5f);
      Stepper_SetDirection(motor_id, STEPPER_DIRECTION_CW);
    }
    else if (delta_angle < -STEPPER_POSITION_DEADBAND_DEG)
    {
        if (current_angle <=
          (-(motor_id == STEPPER_MOTOR_BASE ? STEPPER_BASE_LIMIT_DEG :
                            STEPPER_PITCH_LIMIT_DEG) + STEPPER_ANGLE_PER_PULSE))
      {
        Stepper_Stop(motor_id);
        motor->in_position = 1U;
        continue;
      }
      target_steps = (int32_t)(-delta_angle / STEPPER_ANGLE_PER_PULSE + 0.5f);
      Stepper_SetDirection(motor_id, STEPPER_DIRECTION_CCW);
    }
    else
    {
      Stepper_Stop(motor_id);
      motor->in_position = 1U;
      continue;
    }

    if ((target_steps > 0) && (motor->remaining_steps == 0))
    {
      if (target_steps > STEPPER_MAX_CONTROL_BATCH_STEPS)
        target_steps = STEPPER_MAX_CONTROL_BATCH_STEPS;
      Stepper_StartBatch(motor_id, (uint32_t)target_steps);
    }
  }
}

void Stepper_MoveSteps(uint32_t motor_id, uint32_t steps, int8_t direction)
{
  if ((motor_id >= STEPPER_MOTOR_COUNT) || (steps == 0U)) return;
  Stepper_SetDirection(motor_id, direction);
  Stepper_StartBatch(motor_id, steps);
}

void Stepper_MoveAngle(uint32_t motor_id, float angle_deg, int8_t direction)
{
  float target_angle;
  if (motor_id >= STEPPER_MOTOR_COUNT) return;
  target_angle = angle_deg < 0.0f ? -angle_deg : angle_deg;
  if (angle_deg < 0.0f) direction = -direction;
  if (target_angle > 0.0f)
  {
    target_angle = *Stepper_CurrentAngle(motor_id) +
        target_angle * (direction >= 0 ? 1.0f : -1.0f);
    Stepper_SetTargetAngle(motor_id, target_angle);
  }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  uint32_t motor_id;
  for (motor_id = 0U; motor_id < STEPPER_MOTOR_COUNT; motor_id++)
  {
    if ((htim->Instance == g_stepper_motor[motor_id].pwm_timer->Instance) &&
        (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) &&
        (g_stepper_motor[motor_id].remaining_steps > 0))
    {
      g_stepper_motor[motor_id].remaining_steps--;
      if (g_stepper_motor[motor_id].remaining_steps == 0)
        Stepper_Stop(motor_id);
    }
  }
}
