#ifndef __DC_MOTOR_H
#define __DC_MOTOR_H

#include <stdint.h>
#include "main.h"
#include "tim.h"

typedef struct
{
  volatile float target_angle_deg;
  volatile float current_angle_deg;
  volatile float speed_deg_s;
  volatile int32_t encoder_count;
  volatile uint32_t max_duty;
  volatile uint32_t output_duty;
} RightTrackState_t;

typedef RightTrackState_t LeftTrackState_t;

extern volatile RightTrackState_t g_right_track;
extern volatile LeftTrackState_t g_left_track;
extern volatile uint8_t g_track_motion_enabled;

void RightTrack_Init(void);
void RightTrack_Update(void);
void RightTrack_SetTargetAngle(float angle_deg);
void LeftTrack_Init(void);
void LeftTrack_Update(void);
void LeftTrack_SetTargetAngle(float angle_deg);

#endif
