/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "stm32f4xx_hal_flash_ex.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
// Circular buffer yapısı
#define UART_BUFFER_SIZE 512

typedef struct {
  uint8_t buffer[UART_BUFFER_SIZE];
  uint16_t head;
  uint16_t tail;
  uint16_t count;
} CircularBuffer_t;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

// Bootloader temel tanımları
#define BOOTLOADER_VERSION        1
#define BOOTLOADER_START_ADDRESS  0x08000000
#define BOOTLOADER_END_ADDRESS    0x08007FFF
#define APPLICATION_START_ADDRESS 0x08008000
#define APPLICATION_END_ADDRESS   0x0807FFFF

#define BOOTLOADER_TIMEOUT_MS 10000 // 10 saniye timeout

// Bootloader komutları
#define CMD_GET_INFO              0x10
#define CMD_ERASE_FLASH           0x11
#define CMD_WRITE_FLASH           0x12
#define CMD_READ_FLASH            0x13
#define CMD_GET_CHECKSUM          0x14
#define CMD_JUMP_TO_APP           0x15

// Bootloader yanıtları
#define RESP_OK                   0x90
#define RESP_ERROR                0x91
#define RESP_INVALID_CMD          0x92

/*
// Flash sector tanımları (STM32F446 için)
#define FLASH_SECTOR_0     0U
#define FLASH_SECTOR_1     1U
#define FLASH_SECTOR_2     2U
#define FLASH_SECTOR_3     3U
#define FLASH_SECTOR_4     4U
#define FLASH_SECTOR_5     5U
#define FLASH_SECTOR_6     6U
#define FLASH_SECTOR_7     7U
*/
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
// Bootloader function prototypes
void Bootloader_Init(void);
uint8_t Bootloader_CheckForUpdate(void);
uint8_t Bootloader_Main(void);
void Bootloader_JumpToApplication(void);
uint8_t Bootloader_EraseFlash(uint32_t start_address, uint32_t size);
uint8_t Bootloader_WriteFlash(uint32_t address, uint8_t *data, uint32_t size);
uint8_t Bootloader_ReadFlash(uint32_t address, uint8_t *data, uint32_t size);
uint32_t Bootloader_CalculateChecksum(uint32_t start_address, uint32_t size);
void Bootloader_SendResponse(uint8_t response);
void Bootloader_SendData(uint8_t *data, uint32_t size);
void Buffer_Put(CircularBuffer_t *buf, uint8_t data);
uint8_t Buffer_Get(CircularBuffer_t *buf, uint8_t *data);
uint16_t Buffer_Available(CircularBuffer_t *buf);
uint8_t Buffer_ReadBytes(uint8_t *data, uint32_t size, uint32_t timeout_ms);
uint8_t Buffer_Peek(CircularBuffer_t *buf, uint16_t index, uint8_t *data);


/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_CNTRL_Pin GPIO_PIN_5
#define LED_CNTRL_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
