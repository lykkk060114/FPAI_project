#ifndef __STEPPER_MOTOR_H
#define __STEPPER_MOTOR_H

#include <stdint.h>
#include "main.h"
#include "tim.h"

#define STEPPER_MOTOR_PITCH 0U
#define STEPPER_MOTOR_BASE  1U
#define STEPPER_MOTOR_COUNT 2U

#define STEPPER_EN_PORT_PITCH  GPIOB
#define STEPPER_EN_PIN_PITCH   GPIO_PIN_0
#define STEPPER_DIR_PORT_PITCH GPIOB
#define STEPPER_DIR_PIN_PITCH  GPIO_PIN_1
#define STEPPER_EN_PORT_BASE   GPIOC
#define STEPPER_EN_PIN_BASE    GPIO_PIN_0
#define STEPPER_DIR_PORT_BASE  GPIOC
#define STEPPER_DIR_PIN_BASE   GPIO_PIN_1

#define STEPPER_DIRECTION_CW   1
#define STEPPER_DIRECTION_CCW -1
#define STEPPER_PITCH_POSITIVE_DIR_LEVEL GPIO_PIN_SET
#define STEPPER_BASE_POSITIVE_DIR_LEVEL  GPIO_PIN_SET

#define STEPPER_STEP_ANGLE_DEG     1.8f
#define STEPPER_MICROSTEP_PER_REV (360.0f / STEPPER_STEP_ANGLE_DEG * 16.0f)
#define STEPPER_ANGLE_PER_PULSE   (360.0f / STEPPER_MICROSTEP_PER_REV)
#define STEPPER_PITCH_LIMIT_DEG   20.0f
#define STEPPER_BASE_LIMIT_DEG    40.0f
#define STEPPER_POSITION_DEADBAND_DEG 0.8f
#define STEPPER_RESTART_ERROR_DEG 1.5f
#define STEPPER_MAX_CONTROL_BATCH_STEPS 1U

typedef struct
{
  volatile float pitch_deg;
  volatile float base_deg;
} GimbalAngleCommand_t;

extern volatile GimbalAngleCommand_t g_gimbal_target_angle_deg;
extern volatile GimbalAngleCommand_t g_gimbal_current_angle_deg;

void Stepper_Init(void);
void Stepper_Stop(uint32_t motor_id);
void Stepper_SetEnable(uint32_t motor_id, uint8_t enable);
void Stepper_SetDirection(uint32_t motor_id, int8_t direction);
void Stepper_SetTargetAngle(uint32_t motor_id, float angle_deg);
void Stepper_SetCurrentAngle(uint32_t motor_id, float angle_deg);
void Stepper_UpdateControl(void);
void Stepper_MoveSteps(uint32_t motor_id, uint32_t steps, int8_t direction);
void Stepper_MoveAngle(uint32_t motor_id, float angle_deg, int8_t direction);

#endif
