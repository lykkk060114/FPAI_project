/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stepper_motor.h"
#include "encoder_pwm.h"
#include "dc_motor.h"
#include "serial_protocol.h"
#include "vision_control.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  CHASSIS_STOP = 0,
  CHASSIS_REMOTE,
  CHASSIS_VISION
} ChassisMode_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CHASSIS_VISION_TIMEOUT_MS 150U
#define CHASSIS_BLUETOOTH_TIMEOUT_MS 200U
#define CHASSIS_COMMAND_PERIOD_MS 20U
#define CHASSIS_REMOTE_STEP_DEG 20.0f
#define CHASSIS_REMOTE_RAMP_STEP_DEG 2.0f
#define CHASSIS_COMBINED_TURN_RATIO 0.5f
#define CHASSIS_MAX_POSITION_ERROR_DEG 30.0f
#define CHASSIS_REMOTE_MAX_POSITION_ERROR_DEG 60.0f
#define CHASSIS_STOP_BRAKE_LOOKAHEAD_S 0.08f
#define CHASSIS_STOP_BRAKE_MAX_OFFSET_DEG 8.0f
#define CHASSIS_STOP_SETTLE_SPEED_DEG_S 3.0f
#define CHASSIS_STOP_SETTLE_ERROR_DEG 1.0f
#define CHASSIS_STOP_SETTLE_TIME_MS 80U
#define CHASSIS_STOP_BRAKE_TIMEOUT_MS 1000U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
volatile ChassisMode_t g_chassis_mode = CHASSIS_STOP;

/* USER CODE END Variables */
/* Definitions for stepTask */
osThreadId_t stepTaskHandle;
const osThreadAttr_t stepTask_attributes = {
  .name = "stepTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for moveTask */
osThreadId_t moveTaskHandle;
const osThreadAttr_t moveTask_attributes = {
  .name = "moveTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for serialTask */
osThreadId_t serialTaskHandle;
const osThreadAttr_t serialTask_attributes = {
  .name = "serialTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void Chassis_UpdateCommand(void);

/* USER CODE END FunctionPrototypes */

void StepMotorTask(void *argument);
void MoveMotorTask(void *argument);
void SerialWireTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of stepTask */
  stepTaskHandle = osThreadNew(StepMotorTask, NULL, &stepTask_attributes);

  /* creation of moveTask */
  moveTaskHandle = osThreadNew(MoveMotorTask, NULL, &moveTask_attributes);

  /* creation of serialTask */
  serialTaskHandle = osThreadNew(SerialWireTask, NULL, &serialTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StepMotorTask */
/**
  * @brief  Function implementing the stepTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StepMotorTask */
void StepMotorTask(void *argument)
{
  /* USER CODE BEGIN StepMotorTask */
  Encoder_PWM_Init();
  Stepper_Init();
  HAL_GPIO_WritePin(LASER_GPIO_Port, LASER_Pin, GPIO_PIN_RESET);

  while ((g_encoder_data[ENCODER_MOTOR_PITCH].valid == 0U) ||
         (g_encoder_data[ENCODER_MOTOR_BASE].valid == 0U))
  {
    osDelay(1);
  }

  Stepper_SetCurrentAngle(STEPPER_MOTOR_PITCH,
                          g_encoder_data[ENCODER_MOTOR_PITCH].angle_deg);
  Stepper_SetCurrentAngle(STEPPER_MOTOR_BASE,
                          g_encoder_data[ENCODER_MOTOR_BASE].angle_deg);
  Stepper_SetTargetAngle(STEPPER_MOTOR_PITCH, 0.0f);
  Stepper_SetTargetAngle(STEPPER_MOTOR_BASE, 0.0f);

  /* Infinite loop */
  for(;;)
  {
    if (g_encoder_data[ENCODER_MOTOR_PITCH].valid != 0U)
    {
      Stepper_SetCurrentAngle(STEPPER_MOTOR_PITCH,
                              g_encoder_data[ENCODER_MOTOR_PITCH].angle_deg);
    }
    if (g_encoder_data[ENCODER_MOTOR_BASE].valid != 0U)
    {
      Stepper_SetCurrentAngle(STEPPER_MOTOR_BASE,
                              g_encoder_data[ENCODER_MOTOR_BASE].angle_deg);
    }
    Stepper_UpdateControl();
    osDelay(1);
  }
  /* USER CODE END StepMotorTask */
}

/* USER CODE BEGIN Header_MoveMotorTask */
/**
* @brief Function implementing the moveTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_MoveMotorTask */
void MoveMotorTask(void *argument)
{
  /* USER CODE BEGIN MoveMotorTask */
  RightTrack_Init();
  LeftTrack_Init();
  Bluetooth_Init();
  VisionControl_Init();
  RightTrack_SetTargetAngle(g_right_track.current_angle_deg);
  LeftTrack_SetTargetAngle(g_left_track.current_angle_deg);

  /* Infinite loop */
  for(;;)
  {
    RightTrack_Update();
    LeftTrack_Update();
    Chassis_UpdateCommand();
    osDelay(1);
  }
  /* USER CODE END MoveMotorTask */
}

/* USER CODE BEGIN Header_SerialWireTask */
/**
* @brief Function implementing the serialTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_SerialWireTask */
void SerialWireTask(void *argument)
{
  /* USER CODE BEGIN SerialWireTask */
  SerialProtocol_Init();

  /* Infinite loop */
  for(;;)
  {
    SerialProtocol_OnRxDmaEvent();
    SerialProtocol_TargetIsFresh(150U);
    osDelay(10);
  }
  /* USER CODE END SerialWireTask */
}

static float Chassis_Clamp(float value, float minimum, float maximum)
{
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static float Chassis_LimitTarget(float target, float current, float limit)
{
  return Chassis_Clamp(target, current - limit, current + limit);
}

static uint8_t Chassis_IsRemoteMotionCommand(uint8_t command)
{
  return (command == 'W') || (command == 'S') ||
         (command == 'A') || (command == 'D') ||
         (command == 'Q') || (command == 'E') ||
         (command == 'Z') || (command == 'C');
}

static void Chassis_UpdateCommand(void)
{
  static ChassisMode_t last_mode = CHASSIS_STOP;
  static uint32_t last_command_tick;
  static uint32_t brake_start_tick;
  static uint32_t settle_start_tick;
  static float remote_step_deg;
  static float brake_left_target_deg;
  static float brake_right_target_deg;
  static uint8_t braking_active;
  static uint8_t settle_timer_active;
  static uint8_t last_remote_was_pure_turn;
  uint32_t now = HAL_GetTick();
  uint8_t command;
  uint8_t vision_fresh;
  uint8_t vision_distance_available;
  uint8_t bluetooth_fresh;
  uint8_t vision_turn_requested;
  uint8_t motion_requested;
  uint8_t behavior;
  float position_error_limit;
  float left_forward_step;
  float right_forward_step;
  float left_turn_step;
  float right_turn_step;
  float left_brake_offset;
  float right_brake_offset;
  ChassisMode_t mode;

  taskENTER_CRITICAL();
  vision_fresh = (g_serial_target.valid != 0U) &&
      ((uint32_t)(now - g_serial_target.last_valid_tick_ms) <=
       CHASSIS_VISION_TIMEOUT_MS);
    vision_distance_available = (g_serial_target.distance_m > 0.0f) &&
      (g_serial_target.distance_m < 1000.0f);
    behavior = g_serial_target.behavior;
  bluetooth_fresh = (g_bluetooth_command.valid != 0U) &&
      ((uint32_t)(now - g_bluetooth_command.last_tick_ms) <=
       CHASSIS_BLUETOOTH_TIMEOUT_MS);
  command = g_bluetooth_command.command;
  taskEXIT_CRITICAL();

  mode = vision_fresh ? CHASSIS_VISION :
      (bluetooth_fresh ? CHASSIS_REMOTE : CHASSIS_STOP);
  g_chassis_mode = mode;

    HAL_GPIO_WritePin(LASER_GPIO_Port, LASER_Pin,
      (vision_fresh != 0U) &&
      ((behavior == SERIAL_BEHAVIOR_DRINKING)) ? GPIO_PIN_SET : GPIO_PIN_RESET);

  if ((last_mode == CHASSIS_VISION) && (mode != CHASSIS_VISION))
  {
    Stepper_SetTargetAngle(STEPPER_MOTOR_BASE,
                           g_gimbal_current_angle_deg.base_deg);
    Stepper_SetTargetAngle(STEPPER_MOTOR_PITCH,
                           g_gimbal_current_angle_deg.pitch_deg);
  }

  if ((mode == CHASSIS_VISION) && (mode != last_mode))
  {
    VisionControl_Init();
    remote_step_deg = 0.0f;
    last_command_tick = now - CHASSIS_COMMAND_PERIOD_MS;
    last_mode = mode;
  }

  if (mode == CHASSIS_VISION)
  {
    taskENTER_CRITICAL();
    VisionControl_Update();
    taskEXIT_CRITICAL();
  }

    vision_turn_requested = (bluetooth_fresh != 0U) &&
      ((command == 'A') || (command == 'D'));
    motion_requested = (mode == CHASSIS_VISION) ?
      ((vision_distance_available != 0U) || (vision_turn_requested != 0U)) :
      ((mode == CHASSIS_REMOTE) && Chassis_IsRemoteMotionCommand(command));

  if (motion_requested == 0U)
  {
    remote_step_deg = 0.0f;

    if ((g_track_motion_enabled != 0U) && (braking_active == 0U))
    {
      if ((last_mode == CHASSIS_REMOTE) &&
          (last_remote_was_pure_turn != 0U))
      {
        LeftTrack_SetTargetAngle(g_left_track.current_angle_deg);
        RightTrack_SetTargetAngle(g_right_track.current_angle_deg);
        g_track_motion_enabled = 0U;
        last_remote_was_pure_turn = 0U;
        last_mode = mode;
        return;
      }

      left_brake_offset = Chassis_Clamp(
          g_left_track.speed_deg_s * CHASSIS_STOP_BRAKE_LOOKAHEAD_S,
          -CHASSIS_STOP_BRAKE_MAX_OFFSET_DEG,
          CHASSIS_STOP_BRAKE_MAX_OFFSET_DEG);
      right_brake_offset = Chassis_Clamp(
          g_right_track.speed_deg_s * CHASSIS_STOP_BRAKE_LOOKAHEAD_S,
          -CHASSIS_STOP_BRAKE_MAX_OFFSET_DEG,
          CHASSIS_STOP_BRAKE_MAX_OFFSET_DEG);
      brake_left_target_deg = g_left_track.current_angle_deg + left_brake_offset;
      brake_right_target_deg = g_right_track.current_angle_deg + right_brake_offset;
      LeftTrack_SetTargetAngle(brake_left_target_deg);
      RightTrack_SetTargetAngle(brake_right_target_deg);
      braking_active = 1U;
      brake_start_tick = now;
      settle_timer_active = 0U;
    }

    if (braking_active != 0U)
    {
      uint8_t settled =
          (g_left_track.speed_deg_s < CHASSIS_STOP_SETTLE_SPEED_DEG_S) &&
          (g_left_track.speed_deg_s > -CHASSIS_STOP_SETTLE_SPEED_DEG_S) &&
          (g_right_track.speed_deg_s < CHASSIS_STOP_SETTLE_SPEED_DEG_S) &&
          (g_right_track.speed_deg_s > -CHASSIS_STOP_SETTLE_SPEED_DEG_S) &&
          ((brake_left_target_deg - g_left_track.current_angle_deg) < CHASSIS_STOP_SETTLE_ERROR_DEG) &&
          ((brake_left_target_deg - g_left_track.current_angle_deg) > -CHASSIS_STOP_SETTLE_ERROR_DEG) &&
          ((brake_right_target_deg - g_right_track.current_angle_deg) < CHASSIS_STOP_SETTLE_ERROR_DEG) &&
          ((brake_right_target_deg - g_right_track.current_angle_deg) > -CHASSIS_STOP_SETTLE_ERROR_DEG);

      if (settled != 0U)
      {
        if (settle_timer_active == 0U)
        {
          settle_start_tick = now;
          settle_timer_active = 1U;
        }
        else if ((uint32_t)(now - settle_start_tick) >= CHASSIS_STOP_SETTLE_TIME_MS)
        {
          braking_active = 0U;
          settle_timer_active = 0U;
          g_track_motion_enabled = 0U;
        }
      }
      else
      {
        settle_timer_active = 0U;
      }

      if ((uint32_t)(now - brake_start_tick) >= CHASSIS_STOP_BRAKE_TIMEOUT_MS)
      {
        braking_active = 0U;
        settle_timer_active = 0U;
        g_track_motion_enabled = 0U;
      }
    }
    else
    {
      g_track_motion_enabled = 0U;
      LeftTrack_SetTargetAngle(g_left_track.current_angle_deg);
      RightTrack_SetTargetAngle(g_right_track.current_angle_deg);
    }

    last_mode = mode;
    return;
  }

  if ((mode != last_mode) || (braking_active != 0U))
  {
    g_track_motion_enabled = 0U;
    LeftTrack_SetTargetAngle(g_left_track.current_angle_deg);
    RightTrack_SetTargetAngle(g_right_track.current_angle_deg);
    if (mode == CHASSIS_REMOTE)
    {
      remote_step_deg = 0.0f;
      last_remote_was_pure_turn = 0U;
    }
    braking_active = 0U;
    settle_timer_active = 0U;
    last_command_tick = now - CHASSIS_COMMAND_PERIOD_MS;
    last_mode = mode;
  }

  if ((uint32_t)(now - last_command_tick) < CHASSIS_COMMAND_PERIOD_MS)
    return;
  last_command_tick = now;

  if (mode == CHASSIS_VISION)
  {
    left_turn_step = 0.0f;
    right_turn_step = 0.0f;
    if (vision_turn_requested != 0U)
    {
      remote_step_deg += CHASSIS_REMOTE_RAMP_STEP_DEG;
      if (remote_step_deg > CHASSIS_REMOTE_STEP_DEG)
        remote_step_deg = CHASSIS_REMOTE_STEP_DEG;
      if (command == 'A')
      {
        left_turn_step = -remote_step_deg;
        right_turn_step = remote_step_deg;
      }
      else
      {
        left_turn_step = remote_step_deg;
        right_turn_step = -remote_step_deg;
      }
    }
    else
    {
      remote_step_deg = 0.0f;
    }

    if ((vision_distance_available != 0U) ||
        (vision_turn_requested != 0U))
    {
      LeftTrack_SetTargetAngle(Chassis_LimitTarget(
          g_left_track.target_angle_deg + left_turn_step,
          g_left_track.current_angle_deg,
          CHASSIS_MAX_POSITION_ERROR_DEG));
      RightTrack_SetTargetAngle(Chassis_LimitTarget(
          g_right_track.target_angle_deg + right_turn_step,
          g_right_track.current_angle_deg,
          CHASSIS_MAX_POSITION_ERROR_DEG));
      g_track_motion_enabled = 1U;
    }
    else
    {
      g_track_motion_enabled = 0U;
    }
    last_mode = mode;
    return;
  }
  else
  {
    remote_step_deg += CHASSIS_REMOTE_RAMP_STEP_DEG;
    if (remote_step_deg > CHASSIS_REMOTE_STEP_DEG)
      remote_step_deg = CHASSIS_REMOTE_STEP_DEG;

    left_forward_step = 0.0f;
    right_forward_step = 0.0f;
    left_turn_step = 0.0f;
    right_turn_step = 0.0f;
    last_remote_was_pure_turn = 0U;
    if (command == 'W') { left_forward_step = remote_step_deg; right_forward_step = remote_step_deg; }
    else if (command == 'S') { left_forward_step = -remote_step_deg; right_forward_step = -remote_step_deg; }
    else if (command == 'A') { left_turn_step = -remote_step_deg; right_turn_step = remote_step_deg; last_remote_was_pure_turn = 1U; }
    else if (command == 'D') { left_turn_step = remote_step_deg; right_turn_step = -remote_step_deg; last_remote_was_pure_turn = 1U; }
    else if (command == 'Q') { left_forward_step = (1.0f - CHASSIS_COMBINED_TURN_RATIO) * remote_step_deg; right_forward_step = (1.0f + CHASSIS_COMBINED_TURN_RATIO) * remote_step_deg; }
    else if (command == 'E') { left_forward_step = (1.0f + CHASSIS_COMBINED_TURN_RATIO) * remote_step_deg; right_forward_step = (1.0f - CHASSIS_COMBINED_TURN_RATIO) * remote_step_deg; }
    else if (command == 'Z') { left_forward_step = -(1.0f + CHASSIS_COMBINED_TURN_RATIO) * remote_step_deg; right_forward_step = -(1.0f - CHASSIS_COMBINED_TURN_RATIO) * remote_step_deg; }
    else if (command == 'C') { left_forward_step = -(1.0f - CHASSIS_COMBINED_TURN_RATIO) * remote_step_deg; right_forward_step = -(1.0f + CHASSIS_COMBINED_TURN_RATIO) * remote_step_deg; }
    LeftTrack_SetTargetAngle(g_left_track.target_angle_deg + left_forward_step + left_turn_step);
    RightTrack_SetTargetAngle(g_right_track.target_angle_deg + right_forward_step + right_turn_step);
  }

  position_error_limit = mode == CHASSIS_REMOTE ?
      CHASSIS_REMOTE_MAX_POSITION_ERROR_DEG : CHASSIS_MAX_POSITION_ERROR_DEG;
  LeftTrack_SetTargetAngle(Chassis_LimitTarget(g_left_track.target_angle_deg,
                                              g_left_track.current_angle_deg,
                                              position_error_limit));
  RightTrack_SetTargetAngle(Chassis_LimitTarget(g_right_track.target_angle_deg,
                                               g_right_track.current_angle_deg,
                                               position_error_limit));
  g_track_motion_enabled = 1U;
}

/* USER CODE END Application */

