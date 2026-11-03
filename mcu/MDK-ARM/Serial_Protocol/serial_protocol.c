#include "serial_protocol.h"
#include <string.h>
#include "main.h"
#include "usart.h"

#define SERIAL_DMA_BUFFER_SIZE 128U
#define SERIAL_FRAME_SIZE 19U
#define SERIAL_FRAME_VERSION 0x02U
#define SERIAL_FRAME_TYPE_TARGET 0x01U
#define SERIAL_FRAME_PAYLOAD_LENGTH 9U

volatile SerialTarget_t g_serial_target;
volatile SerialProtocolDebug_t g_serial_protocol_debug;
volatile BluetoothCommand_t g_bluetooth_command;
static uint8_t bluetooth_rx_byte;

static uint8_t serial_dma_buffer[SERIAL_DMA_BUFFER_SIZE];
static uint16_t serial_dma_last_index;
static uint8_t serial_frame[SERIAL_FRAME_SIZE];
static uint8_t serial_frame_index;

static uint16_t SerialProtocol_ReadLe16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t SerialProtocol_ReadLe32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint16_t SerialProtocol_Crc16CcittFalse(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= (uint16_t)data[index] << 8U;
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U) :
                              (uint16_t)(crc << 1U);
    }
  }

  return crc;
}

static void SerialProtocol_ProcessFrame(void)
{
  uint16_t crc_received;
  uint16_t crc_calculated;
  uint32_t distance_bits;
  float distance_m;

  if ((serial_frame[2] != SERIAL_FRAME_VERSION) ||
      (serial_frame[3] != SERIAL_FRAME_TYPE_TARGET) ||
      (SerialProtocol_ReadLe16(&serial_frame[6]) != SERIAL_FRAME_PAYLOAD_LENGTH))
  {
    g_serial_protocol_debug.format_error_count++;
    return;
  }

  crc_received = SerialProtocol_ReadLe16(&serial_frame[17]);
  crc_calculated = SerialProtocol_Crc16CcittFalse(serial_frame, 17U);
  g_serial_protocol_debug.last_crc_received = crc_received;
  g_serial_protocol_debug.last_crc_calculated = crc_calculated;
  if (crc_calculated != crc_received)
  {
    g_serial_target.crc_error_count++;
    return;
  }

  distance_bits = SerialProtocol_ReadLe32(&serial_frame[8]);
  memcpy(&distance_m, &distance_bits, sizeof(distance_m));

  g_serial_target.sequence = SerialProtocol_ReadLe16(&serial_frame[4]);
  g_serial_target.distance_m = distance_m;
  g_serial_target.center_x = SerialProtocol_ReadLe16(&serial_frame[12]);
  g_serial_target.center_y = SerialProtocol_ReadLe16(&serial_frame[14]);
  g_serial_target.behavior = serial_frame[16];
  g_serial_target.last_valid_tick_ms = HAL_GetTick();
  g_serial_target.valid = 1U;
  g_serial_target.valid_frame_count++;
}

static void SerialProtocol_PushByte(uint8_t byte)
{
  g_serial_protocol_debug.last_rx_byte = byte;
  g_serial_protocol_debug.received_byte_count++;

  if (serial_frame_index == 0U)
  {
    if (byte == 0xAAU) serial_frame[serial_frame_index++] = byte;
    return;
  }

  if (serial_frame_index == 1U)
  {
    if (byte == 0x55U)
    {
      serial_frame[serial_frame_index++] = byte;
    }
    else
    {
      serial_frame_index = byte == 0xAAU ? 1U : 0U;
      if (serial_frame_index != 0U) serial_frame[0] = byte;
    }
    return;
  }

  serial_frame[serial_frame_index++] = byte;
  if (serial_frame_index >= SERIAL_FRAME_SIZE)
  {
    g_serial_protocol_debug.frame_candidate_count++;
    SerialProtocol_ProcessFrame();
    serial_frame_index = 0U;
  }

  g_serial_protocol_debug.parser_index = serial_frame_index;
}

void SerialProtocol_Init(void)
{
  memset((void *)&g_serial_target, 0, sizeof(g_serial_target));
    memset((void *)&g_serial_protocol_debug, 0,
      sizeof(g_serial_protocol_debug));
  serial_dma_last_index = 0U;
  serial_frame_index = 0U;

  if (HAL_UART_Receive_DMA(&huart2, serial_dma_buffer,
                           SERIAL_DMA_BUFFER_SIZE) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
}

void SerialProtocol_OnRxDmaEvent(void)
{
  uint16_t producer_index;
  uint32_t previous_mask = __get_PRIMASK();

  __disable_irq();

  g_serial_protocol_debug.dma_event_count++;

  producer_index = SERIAL_DMA_BUFFER_SIZE -
      (uint16_t)__HAL_DMA_GET_COUNTER(huart2.hdmarx);
  if (producer_index == SERIAL_DMA_BUFFER_SIZE) producer_index = 0U;
  g_serial_protocol_debug.dma_producer_index = producer_index;

  while (serial_dma_last_index != producer_index)
  {
    SerialProtocol_PushByte(serial_dma_buffer[serial_dma_last_index]);
    g_serial_protocol_debug.parser_index = serial_frame_index;
    serial_dma_last_index++;
    if (serial_dma_last_index >= SERIAL_DMA_BUFFER_SIZE)
    {
      serial_dma_last_index = 0U;
    }
  }
  if (previous_mask == 0U) __enable_irq();
}

uint8_t SerialProtocol_TargetIsFresh(uint32_t timeout_ms)
{
  uint8_t fresh;
  uint32_t previous_mask = __get_PRIMASK();

  __disable_irq();
  fresh = (g_serial_target.valid != 0U) &&
      ((uint32_t)(HAL_GetTick() - g_serial_target.last_valid_tick_ms) <= timeout_ms);
  if (fresh == 0U)
  {
    g_serial_target.valid = 0U;
  }
  if (previous_mask == 0U) __enable_irq();
  return fresh;
}

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2) SerialProtocol_OnRxDmaEvent();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2) SerialProtocol_OnRxDmaEvent();
  else if (huart->Instance == USART3)
  {
    if ((bluetooth_rx_byte == 'W') || (bluetooth_rx_byte == 'S') ||
        (bluetooth_rx_byte == 'A') || (bluetooth_rx_byte == 'D') ||
      (bluetooth_rx_byte == 'Q') || (bluetooth_rx_byte == 'E') ||
      (bluetooth_rx_byte == 'Z') || (bluetooth_rx_byte == 'C') ||
        (bluetooth_rx_byte == 'X'))
    {
      g_bluetooth_command.command = bluetooth_rx_byte;
      g_bluetooth_command.last_tick_ms = HAL_GetTick();
      g_bluetooth_command.valid = 1U;
    }
    else
    {
      g_bluetooth_command.valid = 0U;
    }
    if (HAL_UART_Receive_IT(&huart3, &bluetooth_rx_byte, 1U) != HAL_OK)
    {
      g_bluetooth_command.valid = 0U;
    }
  }
}

void Bluetooth_Init(void)
{
  g_bluetooth_command.command = 'X';
  g_bluetooth_command.valid = 0U;
  g_bluetooth_command.last_tick_ms = 0U;
  if (HAL_UART_Receive_IT(&huart3, &bluetooth_rx_byte, 1U) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    g_bluetooth_command.valid = 0U;
    (void)HAL_UART_Receive_IT(&huart3, &bluetooth_rx_byte, 1U);
  }
}
