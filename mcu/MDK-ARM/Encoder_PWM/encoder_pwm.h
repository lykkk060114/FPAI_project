#ifndef __ENCODER_PWM_H
#define __ENCODER_PWM_H

#include <stdint.h>
#include "main.h"
#include "tim.h"

#define ENCODER_MOTOR_PITCH 0U
#define ENCODER_MOTOR_BASE  1U
#define ENCODER_COUNT       2U
#define ENCODER_RAW_ZERO_SNAP_DEG 1.5f
#define ENCODER_PITCH_ZERO_OFFSET_DEG 13.0f
#define ENCODER_BASE_ZERO_OFFSET_DEG  -45.0f

typedef struct
{
  volatile uint32_t period_us;
  volatile uint32_t high_us;
  volatile float duty_percent;
  volatile float raw_angle_deg;
  volatile float zero_offset_deg;
  volatile float angle_deg;
  volatile uint8_t valid;
} EncoderData_t;

extern volatile EncoderData_t g_encoder_data[ENCODER_COUNT];

void Encoder_PWM_Init(void);
float Encoder_PWM_GetAngle(uint32_t encoder_id);

#endif
