#ifndef __SERIAL_PROTOCOL_H
#define __SERIAL_PROTOCOL_H

#include <stdint.h>

typedef struct
{
  volatile uint32_t dma_event_count;
  volatile uint32_t received_byte_count;
  volatile uint32_t frame_candidate_count;
  volatile uint32_t format_error_count;
  volatile uint16_t last_crc_received;
  volatile uint16_t last_crc_calculated;
  volatile uint16_t dma_producer_index;
  volatile uint8_t last_rx_byte;
  volatile uint8_t parser_index;
} SerialProtocolDebug_t;

typedef enum
{
  SERIAL_BEHAVIOR_DRINKING = 1U,
  SERIAL_BEHAVIOR_EATING = 2U,
  SERIAL_BEHAVIOR_RESTING = 3U,
  SERIAL_BEHAVIOR_WALKING = 4U
} SerialBehavior_t;

typedef struct
{
  volatile uint16_t sequence;
  volatile float distance_m;
  volatile uint16_t center_x;
  volatile uint16_t center_y;
  volatile uint8_t behavior;
  volatile uint8_t valid;
  volatile uint32_t last_valid_tick_ms;
  volatile uint32_t valid_frame_count;
  volatile uint32_t crc_error_count;
} SerialTarget_t;

extern volatile SerialTarget_t g_serial_target;
extern volatile SerialProtocolDebug_t g_serial_protocol_debug;

typedef struct
{
  volatile uint8_t command;
  volatile uint8_t valid;
  volatile uint32_t last_tick_ms;
} BluetoothCommand_t;

extern volatile BluetoothCommand_t g_bluetooth_command;

void SerialProtocol_Init(void);
void SerialProtocol_OnRxDmaEvent(void);
uint8_t SerialProtocol_TargetIsFresh(uint32_t timeout_ms);
void Bluetooth_Init(void);

#endif
