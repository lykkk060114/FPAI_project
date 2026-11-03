#include <assert.h>
#include <math.h>
#include "../Core/Src/freertos.c"

static uint32_t test_tick;
static float vision_test_increment = 100.0f;
static GPIO_PinState laser_state = GPIO_PIN_RESET;
volatile RightTrackState_t g_right_track;
volatile LeftTrackState_t g_left_track;
volatile uint8_t g_track_motion_enabled;
volatile GimbalAngleCommand_t g_gimbal_target_angle_deg;
volatile GimbalAngleCommand_t g_gimbal_current_angle_deg;
volatile SerialTarget_t g_serial_target;
volatile BluetoothCommand_t g_bluetooth_command;
volatile EncoderData_t g_encoder_data[ENCODER_COUNT];

uint32_t HAL_GetTick(void) { return test_tick; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
  if ((port == LASER_GPIO_Port) && (pin == LASER_Pin)) laser_state = state;
}
void vPortEnterCritical(void) {}
void vPortExitCritical(void) {}
osThreadId_t osThreadNew(osThreadFunc_t function, void *argument, const osThreadAttr_t *attr)
{
  (void)function; (void)argument; (void)attr;
  return NULL;
}
osStatus_t osDelay(uint32_t ticks) { (void)ticks; return osOK; }
void Encoder_PWM_Init(void) {}
void Stepper_Init(void) {}
void Stepper_SetCurrentAngle(uint32_t motor_id, float angle) { (void)motor_id; (void)angle; }
void Stepper_SetTargetAngle(uint32_t motor_id, float angle) { (void)motor_id; (void)angle; }
void Stepper_UpdateControl(void) {}
void RightTrack_Init(void) {}
void LeftTrack_Init(void) {}
void RightTrack_Update(void) {}
void LeftTrack_Update(void) {}
void Bluetooth_Init(void) {}
void SerialProtocol_Init(void) {}
void SerialProtocol_OnRxDmaEvent(void) {}
uint8_t SerialProtocol_TargetIsFresh(uint32_t timeout) { (void)timeout; return 0U; }
void LeftTrack_SetTargetAngle(float value) { g_left_track.target_angle_deg = value; }
void RightTrack_SetTargetAngle(float value) { g_right_track.target_angle_deg = value; }
void VisionControl_Init(void) {}
void VisionControl_Update(void)
{
  g_left_track.target_angle_deg += vision_test_increment;
  g_right_track.target_angle_deg += vision_test_increment;
}

int main(void)
{
  test_tick = 1000U;
  Chassis_UpdateCommand();
  assert(g_chassis_mode == CHASSIS_STOP && g_track_motion_enabled == 0U);

  g_bluetooth_command.valid = 1U;
  g_bluetooth_command.command = 'W';
  g_bluetooth_command.last_tick_ms = test_tick;
  Chassis_UpdateCommand();
  assert(g_chassis_mode == CHASSIS_REMOTE && g_track_motion_enabled == 1U);
  assert(g_left_track.target_angle_deg == 2.0f && g_right_track.target_angle_deg == 2.0f);

  test_tick += 20U;
  g_bluetooth_command.command = 'A';
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == -2.0f && g_right_track.target_angle_deg == 6.0f);

  test_tick += 20U;
  g_bluetooth_command.command = 'S';
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == -8.0f && g_right_track.target_angle_deg == 0.0f);

  test_tick += 20U;
  g_bluetooth_command.command = 'D';
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == 0.0f && g_right_track.target_angle_deg == -8.0f);

  test_tick += 20U;
  g_bluetooth_command.command = 'Q';
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == 5.0f && g_right_track.target_angle_deg == 7.0f);

  test_tick += 20U;
  g_bluetooth_command.command = 'Z';
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == -13.0f && g_right_track.target_angle_deg == 1.0f);

  test_tick += 20U;
  g_bluetooth_command.command = 'E';
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == 8.0f && g_right_track.target_angle_deg == 8.0f);

  test_tick += 20U;
  g_bluetooth_command.command = 'C';
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == 0.0f && g_right_track.target_angle_deg == -16.0f);

  g_left_track.speed_deg_s = 50.0f;
  g_right_track.speed_deg_s = 40.0f;
  g_bluetooth_command.command = 'X';
  Chassis_UpdateCommand();
  assert(g_chassis_mode == CHASSIS_REMOTE && g_track_motion_enabled == 1U);
  assert(g_left_track.target_angle_deg == 4.0f);
  assert(fabsf(g_right_track.target_angle_deg - 3.2f) < 0.001f);

  g_left_track.current_angle_deg = g_left_track.target_angle_deg;
  g_right_track.current_angle_deg = g_right_track.target_angle_deg;
  g_left_track.speed_deg_s = 0.0f;
  g_right_track.speed_deg_s = 0.0f;
  for (int step = 0; step < 5; step++)
  {
    test_tick += 20U;
    Chassis_UpdateCommand();
  }
  assert(g_track_motion_enabled == 0U);

  test_tick += 20U;
  g_bluetooth_command.command = 'A';
  g_bluetooth_command.last_tick_ms = test_tick;
  Chassis_UpdateCommand();
  g_left_track.speed_deg_s = -30.0f;
  g_right_track.speed_deg_s = 30.0f;
  g_bluetooth_command.command = 'X';
  Chassis_UpdateCommand();
  assert(g_track_motion_enabled == 0U);
  assert(fabsf(g_left_track.target_angle_deg - g_left_track.current_angle_deg) < 0.001f);
  assert(fabsf(g_right_track.target_angle_deg - g_right_track.current_angle_deg) < 0.001f);

  g_bluetooth_command.command = 'W';
  g_bluetooth_command.last_tick_ms = test_tick;
  for (int step = 0; step < 12; step++)
  {
    test_tick += 20U;
    g_bluetooth_command.last_tick_ms = test_tick;
    Chassis_UpdateCommand();
  }
  assert(g_left_track.target_angle_deg == 64.0f && g_right_track.target_angle_deg == 63.2f);

  g_bluetooth_command.command = 'W';
  g_serial_target.valid = 1U;
  g_serial_target.distance_m = 2.0f;
  g_serial_target.behavior = SERIAL_BEHAVIOR_DRINKING;
  g_serial_target.last_valid_tick_ms = test_tick;
  Chassis_UpdateCommand();
  assert(g_chassis_mode == CHASSIS_VISION && g_track_motion_enabled == 1U);
  assert(laser_state == GPIO_PIN_SET);
  assert(g_left_track.target_angle_deg == g_left_track.current_angle_deg + 30.0f);
  assert(g_right_track.target_angle_deg == g_right_track.current_angle_deg + 30.0f);

  vision_test_increment = 0.0f;
  g_serial_target.distance_m = 0.0f;
  Chassis_UpdateCommand();
  assert(g_chassis_mode == CHASSIS_VISION && g_track_motion_enabled == 1U);

  test_tick += 20U;
  g_serial_target.last_valid_tick_ms = test_tick;
  g_serial_target.behavior = SERIAL_BEHAVIOR_EATING;
  g_bluetooth_command.valid = 1U;
  g_bluetooth_command.command = 'A';
  g_bluetooth_command.last_tick_ms = test_tick;
  g_left_track.current_angle_deg = 0.0f;
  g_right_track.current_angle_deg = 0.0f;
  g_left_track.target_angle_deg = 0.0f;
  g_right_track.target_angle_deg = 0.0f;
  g_left_track.speed_deg_s = 0.0f;
  g_right_track.speed_deg_s = 0.0f;
  Chassis_UpdateCommand();
  assert(g_chassis_mode == CHASSIS_VISION && g_track_motion_enabled == 1U);
  assert(g_left_track.target_angle_deg == -2.0f);
  assert(g_right_track.target_angle_deg == 2.0f);
  assert(laser_state == GPIO_PIN_SET);

  test_tick += 20U;
  g_serial_target.last_valid_tick_ms = test_tick;
  g_serial_target.behavior = SERIAL_BEHAVIOR_RESTING;
  g_bluetooth_command.command = 'W';
  g_bluetooth_command.last_tick_ms = test_tick;
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == g_left_track.current_angle_deg);
  assert(g_right_track.target_angle_deg == g_right_track.current_angle_deg);
  assert(laser_state == GPIO_PIN_RESET);

  test_tick += 20U;
  g_serial_target.last_valid_tick_ms = test_tick;
  g_serial_target.behavior = SERIAL_BEHAVIOR_WALKING;
  g_bluetooth_command.command = 'D';
  g_bluetooth_command.last_tick_ms = test_tick;
  g_left_track.current_angle_deg = 0.0f;
  g_right_track.current_angle_deg = 0.0f;
  g_left_track.target_angle_deg = 0.0f;
  g_right_track.target_angle_deg = 0.0f;
  Chassis_UpdateCommand();
  assert(g_left_track.target_angle_deg == 2.0f);
  assert(g_right_track.target_angle_deg == -2.0f);
  assert(laser_state == GPIO_PIN_RESET);

  g_bluetooth_command.command = 'W';
  test_tick += 151U;
  g_bluetooth_command.last_tick_ms = test_tick;
  Chassis_UpdateCommand();
  assert(g_chassis_mode == CHASSIS_REMOTE && g_track_motion_enabled == 1U);

  test_tick += 201U;
  Chassis_UpdateCommand();
  assert(g_chassis_mode == CHASSIS_STOP && g_track_motion_enabled == 1U);
  g_left_track.current_angle_deg = g_left_track.target_angle_deg;
  g_right_track.current_angle_deg = g_right_track.target_angle_deg;
  g_left_track.speed_deg_s = 0.0f;
  g_right_track.speed_deg_s = 0.0f;
  for (int step = 0; step < 5; step++)
  {
    test_tick += 20U;
    Chassis_UpdateCommand();
  }
  assert(g_track_motion_enabled == 0U);
  return 0;
}