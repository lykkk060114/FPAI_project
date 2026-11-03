#ifndef __VISION_CONTROL_H
#define __VISION_CONTROL_H

#include <stdint.h>

typedef struct
{
  volatile float distance_error_m;
  volatile float forward_angle_increment_deg;
  volatile uint16_t last_sequence;
  volatile uint32_t processed_frame_count;
  volatile uint8_t target_active;
} VisionControlState_t;

extern volatile VisionControlState_t g_vision_control;

void VisionControl_Init(void);
void VisionControl_Update(void);

#endif
