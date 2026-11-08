#include <cstring>

#include "main.h"
#include "cmsis_os2.h"
#include "stdio.h"

#include "spi.h"
#include "adc.h"
#include "tim.h"

#include "CCAN.hpp"
#include "FullColorLED.hpp"
#include "WS2812B.hpp"
#include "stm32g4xx_hal_gpio.h"

#include "PowerBoard_Format.h"

constexpr uint8_t MY_BOARD_NUM = 0x02;

extern DMA_HandleTypeDef hdma_tim2_ch1;
CANFD *canfd;
FullColorLED led{&htim1, TIM_CHANNEL_1};
WS2812B status_LED{&htim2, TIM_CHANNEL_1, &hdma_tim2_ch1};
int data;

extern osTimerId_t dischargeTimerHandle;
extern osTimerId_t cantx_taskHandle;
extern DMA_HandleTypeDef hdma_adc1;

bool onoff = false;
bool discharge = false;
bool emengecy = false;

#define MCP3208_CS_LOW() HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET)
#define MCP3208_CS_HIGH() HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET)

#define MCP3208_START_BIT 0x04
#define MCP3208_MODE_SINGLE 0x02
#define MCP3208_MODE_DIFF 0x00

uint16_t ADC_buff[3];

float output_voltage = 0.0f;
int rx_time = 0;

uint16_t MCP3208_Read(uint8_t channel)
{
  uint8_t tx[3];
  uint8_t rx[3];
  MCP3208_CS_LOW();
  tx[0] = MCP3208_START_BIT | MCP3208_MODE_SINGLE | ((channel & 0x04) >> 2);
  tx[1] = (channel & 0x03) << 6;
  tx[2] = 0;
  HAL_SPI_TransmitReceive(&hspi1, tx, rx, 3, HAL_MAX_DELAY);
  MCP3208_CS_HIGH();
  uint16_t value;
  value = ((rx[1] & 0x0F) << 8) | rx[2];
  return value;
}

void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim)
{
  if (htim == &htim2)
  {
    status_LED.do_forwardRewrite();
  }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (htim == &htim2)
  {
    status_LED.do_backRewrite();
  }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
  if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET)
  {
    canfd->rx_interrupt_task();
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == EMENGECY_Pin)
  {
    if (HAL_GPIO_ReadPin(EMENGECY_GPIO_Port, EMENGECY_Pin) == GPIO_PIN_RESET)
    {
      HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_RESET);
      if (onoff)
      {
        led.set_rgb(0, 255, 0);
      }
      else
      {
        led.set_rgb(255, 0, 0);
      }
      emengecy = false;
    }
    else
    {
      HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_SET);
      led.set_rgb(100, 50, 0);
      emengecy = true;
      osTimerStart(dischargeTimerHandle, 300);
    }
  }
}

extern "C" void StartDefaultTask(void *argument)
{

  canfd = new CANFD(&hfdcan1);
  canfd->start();

  // while(!canfd->rx_available()) osDelay(100);
  led.set_rgb(255, 0, 0);
  led.start();

  osTimerStart(cantx_taskHandle, 100);
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)ADC_buff, sizeof(ADC_buff) / sizeof(ADC_buff[0]));
  hdma_adc1.Instance->CCR &= ~(DMA_IT_TC | DMA_IT_HT);

  // 自身宛て (Data Type = 0x02, Board Num = 自身の基板番号)
  uint32_t own_filter_id = can_format::make_can_id(0, can_format::DATA_TYPE_POWER_BOARD, MY_BOARD_NUM, 0);
  uint32_t own_filter_mask = 0x00FF0000; // Data Type と Board Num の8bitを厳密に一致させるマスク
  canfd->set_filter_mask(0, own_filter_id, own_filter_mask);

  // ブロードキャスト共通コマンド (Data Type = 0x01)
  uint32_t bcast_filter_id = can_format::make_can_id(0, can_format::DATA_TYPE_COMMON, 0x01, 0);
  uint32_t bcast_filter_mask = 0x00FF0000;
  canfd->set_filter_mask(1, bcast_filter_id, bcast_filter_mask);

  while (1)
  {
    if (canfd->rx_available())
    {
      CANFD_Frame frame;
      canfd->rx(frame);

      uint8_t dtype = can_format::get_data_type(frame.id);
      uint16_t reg_id = can_format::get_register_id(frame.id);

      // 非常停止（共通コマンド）
      // 非常停止（共通コマンド: 0x01, REG_EMS: 0x0001）
      if (dtype == can_format::DATA_TYPE_COMMON && reg_id == can_format::common::REG_EMS)
      {
        bool ems_active = (frame.data[0] != 0);
        if (ems_active)
        {
          // 【非常停止】MCON遮断、放電開始
          HAL_GPIO_WritePin(ONOFF_GPIO_Port, ONOFF_Pin, GPIO_PIN_RESET);
          osDelay(30);
          HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_SET);
          osTimerStart(dischargeTimerHandle, 300);
          onoff = false;
          emengecy = true;
          led.set_rgb(255, 0, 0); // 赤色
        }
        else
        {
          // 【非常停止解除】物理スイッチが押されていないか確認して電源投入
          if (HAL_GPIO_ReadPin(EMENGECY_GPIO_Port, EMENGECY_Pin) == GPIO_PIN_RESET)
          {
            HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_RESET); // 放電停止
            osDelay(10);
            HAL_GPIO_WritePin(ONOFF_GPIO_Port, ONOFF_Pin, GPIO_PIN_SET); // ★ MCON ON！
            onoff = true;
            emengecy = false;
            led.set_rgb(0, 255, 0); // 緑色
          }
          else
          {
            // 物理非常停止ボタンが押されている場合は投入しない
            led.set_rgb(100, 50, 0); // オレンジ
          }
        }
      }
      // 電源基板向けコマンド
      else if (dtype == can_format::DATA_TYPE_POWER_BOARD)
      {
        switch (reg_id)
        {
        case can_format::powerboard::REG_REQ_REMS_LED:
        {
          bool rems = (frame.data[0] != 0);
          // 遠隔非常停止LED表示
          break;
        }
        case can_format::powerboard::REG_REQ_STATE_LED_AUTOPILOT:
        case can_format::powerboard::REG_REQ_STATE_LED_MANUAL:
        case can_format::powerboard::REG_REQ_STATE_LED_GAMING:
        case can_format::powerboard::REG_REQ_STATE_LED_PACHINKO:
        {
          // LEDパターンの変更（status_LED / WS2812B のモード切替）
          break;
        }
        case can_format::powerboard::REG_SET_MONITOR_PERIOD:
        {
          uint16_t period_ms;
          memcpy(&period_ms, frame.data, sizeof(period_ms));
          if (period_ms > 0)
          {
            osTimerStop(cantx_taskHandle);
            osTimerStart(cantx_taskHandle, period_ms);
          }
          else if (period_ms == 0)
          {
            osTimerStop(cantx_taskHandle); // 周期送信停止
          }
          break;
        }
        default:
          break;
        }
      }
      rx_time = HAL_GetTick();
    }

    if (HAL_GetTick() - rx_time > 1000)
    {
      led.set_rgb(255, 0, 0);
      HAL_GPIO_WritePin(ONOFF_GPIO_Port, ONOFF_Pin, GPIO_PIN_RESET);
      osDelay(30);
      HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_SET);
      osTimerStart(dischargeTimerHandle, 300);
      onoff = false;
    }
    osDelay(10);
  }
}

extern "C" void cantxCallback(void *argument)
{
  // 1. センサ読み取り
  float bat_v = MCP3208_Read(0) / 122.0f;      // 電圧
  float current = (ADC_buff[0] - 410) / 62.0f; // 電流

  if (current > 20.0f)
  {
    HAL_GPIO_WritePin(ONOFF_GPIO_Port, ONOFF_Pin, GPIO_PIN_RESET);
    onoff = false;
  }
  // 2. 電圧送信 (REG_GET_BAT_V: 0x0030)
  {
    CANFD_Frame tx_v;
    tx_v.id = can_format::make_can_id(
        can_format::PRIORITY_NORMAL,
        can_format::DATA_TYPE_POWER_BOARD,
        MY_BOARD_NUM,
        can_format::powerboard::REG_GET_BAT_V);
    tx_v.size = sizeof(float);
    memcpy(tx_v.data, &bat_v, sizeof(float));
    canfd->tx(tx_v);
  }

  // 3. 電流送信 (REG_GET_CURRENT: 0x0031)
  {
    CANFD_Frame tx_i;
    tx_i.id = can_format::make_can_id(
        can_format::PRIORITY_NORMAL,
        can_format::DATA_TYPE_POWER_BOARD,
        MY_BOARD_NUM,
        can_format::powerboard::REG_GET_CURRENT);
    tx_i.size = sizeof(float);
    memcpy(tx_i.data, &current, sizeof(float));
    canfd->tx(tx_i);
  }
}

extern "C" void dischargeCallback(void *argument)
{
  HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_RESET);
}

void rainbow(int count, uint8_t *rgb)
{
  int x = count / 255;
  int y = count % 255;

  if (x == 0)
    rgb[0] = 255, rgb[1] = y, rgb[2] = 0;
  else if (x == 1)
    rgb[0] = 255 - y, rgb[1] = 255, rgb[2] = 0;
  else if (x == 2)
    rgb[0] = 0, rgb[1] = 255, rgb[2] = y;
  else if (x == 3)
    rgb[0] = 0, rgb[1] = 255 - y, rgb[2] = 255;
  else if (x == 4)
    rgb[0] = y, rgb[1] = 0, rgb[2] = 255;
  else if (x == 5)
    rgb[0] = 255, rgb[1] = 0, rgb[2] = 255 - y;

  // 彩度を少し強める（100 = 1.0倍）
  constexpr int saturation_scale = 120;
  const int gray = (static_cast<int>(rgb[0]) + static_cast<int>(rgb[1]) + static_cast<int>(rgb[2])) / 3;

  for (int i = 0; i < 3; i++)
  {
    int v = gray + ((static_cast<int>(rgb[i]) - gray) * saturation_scale) / 100;
    if (v < 0)
      v = 0;
    else if (v > 255)
      v = 255;
    rgb[i] = static_cast<uint8_t>(v);
  }
}

int count = 0;

extern "C" void controllLEDTask(void *argument)
{
  enum class LedState
  {
    OFF,
    AUTOPILOT,
    MANUAL,
    GAMING,
    PACHINKO,
    EMERGENCY
  };
  volatile LedState current_led_state = LedState::MANUAL;

  while (1)
  {
    osDelay(100);
  }
}