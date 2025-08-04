/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static CircularBuffer_t uart_rx_buffer = {0};
static uint8_t uart_rx_byte;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  // Bootloader başlatma
  Bootloader_Init();

  // LED'i yak (bootloader çalışıyor göstergesi)
  HAL_GPIO_WritePin(LED_CNTRL_GPIO_Port, LED_CNTRL_Pin, GPIO_PIN_SET);

  // Basit test mesajı gönder
  uint8_t msg[] = "STM32F446 Bootloader Ready (10s timeout)\r\n";
  HAL_UART_Transmit(&huart2, msg, sizeof(msg)-1, 1000);
  
  // Bootloader timeout ayarları
  uint32_t bootloader_start_time = HAL_GetTick();
  uint32_t led_last_toggle = HAL_GetTick();
  uint8_t led_state = 1;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t current_time = HAL_GetTick();
    
    // LED blink (bootloader aktif göstergesi - her 200ms)
    if ((current_time - led_last_toggle) > 200)
    {
      led_state = !led_state;
      HAL_GPIO_WritePin(LED_CNTRL_GPIO_Port, LED_CNTRL_Pin, led_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
      led_last_toggle = current_time;
    }
    
    // Timeout kontrolü
    if ((current_time - bootloader_start_time) > BOOTLOADER_TIMEOUT_MS)
    {
      // Timeout mesajı
      uint8_t timeout_msg[] = "Bootloader timeout, checking for application...\r\n";
      HAL_UART_Transmit(&huart2, timeout_msg, sizeof(timeout_msg)-1, 1000);
      
      // Application'da geçerli kod var mı kontrol et
      uint32_t app_stack_ptr = *(volatile uint32_t*)APPLICATION_START_ADDRESS;
      uint32_t app_reset_handler = *(volatile uint32_t*)(APPLICATION_START_ADDRESS + 4);
      
      // Application validation
      if (app_stack_ptr >= 0x20000000 && app_stack_ptr <= 0x20040000 &&
          app_reset_handler != 0xFFFFFFFF &&
          app_reset_handler >= APPLICATION_START_ADDRESS &&
          app_reset_handler < 0x08080000)
      {
        // LED'i söndür
        HAL_GPIO_WritePin(LED_CNTRL_GPIO_Port, LED_CNTRL_Pin, GPIO_PIN_RESET);
        
        uint8_t jump_msg[] = "Jumping to application...\r\n";
        HAL_UART_Transmit(&huart2, jump_msg, sizeof(jump_msg)-1, 1000);
        HAL_Delay(100);
        
        // Application'a atla
        Bootloader_JumpToApplication();
      }
      else
      {
        uint8_t no_app_msg[] = "No valid application found, staying in bootloader\r\n";
        HAL_UART_Transmit(&huart2, no_app_msg, sizeof(no_app_msg)-1, 1000);
        
        // Timer'ı reset et, bootloader'da kal
        bootloader_start_time = current_time;
      }
    }
    
    // UART komut kontrolü
    if (Bootloader_CheckForUpdate())
    {
      // Aktivite algılandı, timer'ı reset et
      bootloader_start_time = current_time;
      
      // LED'i açık tut (aktivite göstergesi)
      HAL_GPIO_WritePin(LED_CNTRL_GPIO_Port, LED_CNTRL_Pin, GPIO_PIN_SET);
      led_state = 1;
      led_last_toggle = current_time;
      
      uint8_t should_continue = Bootloader_Main();
      
      if (should_continue == 0) {
        break; // Jump komutu geldi, çık
      }
    }

    // Kısa bekle
    HAL_Delay(10);
  }
  
  // Bootloader sonlandı, application çalışıyor
  while(1) {
    // Boş loop - asla buraya ulaşılmamalı
    HAL_Delay(1000);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 84;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_CNTRL_GPIO_Port, LED_CNTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_CNTRL_Pin */
  GPIO_InitStruct.Pin = LED_CNTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_CNTRL_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
 * @brief Bootloader initialization
 */
void Bootloader_Init(void)
{
  // Circular buffer'ı sıfırla
  uart_rx_buffer.head = 0;
  uart_rx_buffer.tail = 0;
  uart_rx_buffer.count = 0;

  // UART interrupt reception başlat
  HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
}

/**
 * @brief Check for incoming UART data
 */
uint8_t Bootloader_CheckForUpdate(void)
{
  return Buffer_Available(&uart_rx_buffer) > 0;
}

/**
 * @brief Buffer'dan belirtilen sayıda byte oku (Little Endian)
 */
uint8_t Buffer_ReadBytes(uint8_t *data, uint32_t size, uint32_t timeout_ms)
{
  uint32_t start_time = HAL_GetTick();
  uint32_t bytes_read = 0;

  while (bytes_read < size) {
    if (Buffer_Available(&uart_rx_buffer) > 0) {
      Buffer_Get(&uart_rx_buffer, &data[bytes_read]);
      bytes_read++;
    }

    // Timeout kontrolü
    if ((HAL_GetTick() - start_time) > timeout_ms) {
      return 0; // Timeout
    }

    HAL_Delay(1);
  }

  return 1; // Başarılı
}

/**
 * @brief Main bootloader command processor
 * @return 1: Continue loop, 0: Exit loop (jump to app)
 */
uint8_t Bootloader_Main(void)
{
  uint8_t command;

  // Komut byte'ını al
  if (!Buffer_ReadBytes(&command, 1, 100)) {
    return 1; // Timeout, continue loop
  }

  switch(command)
  {
    case CMD_GET_INFO:
    {
      uint8_t response[6];
      response[0] = RESP_OK;
      response[1] = BOOTLOADER_VERSION;
      response[2] = (APPLICATION_START_ADDRESS >> 24) & 0xFF;
      response[3] = (APPLICATION_START_ADDRESS >> 16) & 0xFF;
      response[4] = (APPLICATION_START_ADDRESS >> 8) & 0xFF;
      response[5] = APPLICATION_START_ADDRESS & 0xFF;

      HAL_UART_Transmit(&huart2, response, 6, 1000);
      return 1; // Continue loop
    }

    case CMD_READ_FLASH:
    {
      uint32_t address = 0;
      uint32_t size = 0;
      uint8_t data[256] = {0};
      uint8_t addr_bytes[4];
      uint8_t size_bytes[4];

      // Address al (4 byte, little endian)
      if (!Buffer_ReadBytes(addr_bytes, 4, 1000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }
      
      // Little endian'dan uint32_t'ye çevir
      address = (uint32_t)addr_bytes[0] | ((uint32_t)addr_bytes[1] << 8) | 
                ((uint32_t)addr_bytes[2] << 16) | ((uint32_t)addr_bytes[3] << 24);

      // Size al (4 byte, little endian)
      if (!Buffer_ReadBytes(size_bytes, 4, 1000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }
      
      // Little endian'dan uint32_t'ye çevir
      size = (uint32_t)size_bytes[0] | ((uint32_t)size_bytes[1] << 8) | 
             ((uint32_t)size_bytes[2] << 16) | ((uint32_t)size_bytes[3] << 24);

      // Boyut kontrolü
      if (size > 256) {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }

      // Flash'tan oku
      if (Bootloader_ReadFlash(address, data, size) == 0)
      {
        uint8_t ok = RESP_OK;
        HAL_UART_Transmit(&huart2, &ok, 1, 1000);
        HAL_UART_Transmit(&huart2, data, size, 1000);
      }
      else
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
      }
      return 1; // Continue loop
    }

    case CMD_ERASE_FLASH:
    {
      uint32_t address;
      uint32_t size;
      uint8_t addr_bytes[4];
      uint8_t size_bytes[4];

      // Address al (4 byte, little endian)
      if (!Buffer_ReadBytes(addr_bytes, 4, 1000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }
      
      // Little endian'dan uint32_t'ye çevir
      address = (uint32_t)addr_bytes[0] | ((uint32_t)addr_bytes[1] << 8) | 
                ((uint32_t)addr_bytes[2] << 16) | ((uint32_t)addr_bytes[3] << 24);

      // Size al (4 byte, little endian)
      if (!Buffer_ReadBytes(size_bytes, 4, 1000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }
      
      // Little endian'dan uint32_t'ye çevir
      size = (uint32_t)size_bytes[0] | ((uint32_t)size_bytes[1] << 8) | 
             ((uint32_t)size_bytes[2] << 16) | ((uint32_t)size_bytes[3] << 24);

      // Flash'ı sil
      if (Bootloader_EraseFlash(address, size) == 0)
      {
        uint8_t ok = RESP_OK;
        HAL_UART_Transmit(&huart2, &ok, 1, 1000);
      }
      else
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
      }
      return 1; // Continue loop
    }

    case CMD_WRITE_FLASH:
    {
      uint32_t address;
      uint32_t size;
      uint8_t data[256];
      uint8_t addr_bytes[4];
      uint8_t size_bytes[4];

      // Address al (4 byte, little endian)
      if (!Buffer_ReadBytes(addr_bytes, 4, 1000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }
      
      // Little endian'dan uint32_t'ye çevir
      address = (uint32_t)addr_bytes[0] | ((uint32_t)addr_bytes[1] << 8) | 
                ((uint32_t)addr_bytes[2] << 16) | ((uint32_t)addr_bytes[3] << 24);

      // Size al (4 byte, little endian)
      if (!Buffer_ReadBytes(size_bytes, 4, 1000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }
      
      // Little endian'dan uint32_t'ye çevir
      size = (uint32_t)size_bytes[0] | ((uint32_t)size_bytes[1] << 8) | 
             ((uint32_t)size_bytes[2] << 16) | ((uint32_t)size_bytes[3] << 24);

      // Boyut kontrolü
      if (size > 256) {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }

      // Data al (size byte)
      if (!Buffer_ReadBytes(data, size, 2000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }

      // Flash'a yaz
      if (Bootloader_WriteFlash(address, data, size) == 0)
      {
        uint8_t ok = RESP_OK;
        HAL_UART_Transmit(&huart2, &ok, 1, 1000);
      }
      else
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
      }
      return 1; // Continue loop
    }

    case CMD_GET_CHECKSUM:
    {
      uint32_t address;
      uint32_t size;
      uint8_t addr_bytes[4];
      uint8_t size_bytes[4];

      // Address al (4 byte, little endian)
      if (!Buffer_ReadBytes(addr_bytes, 4, 1000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }
      
      // Little endian'dan uint32_t'ye çevir
      address = (uint32_t)addr_bytes[0] | ((uint32_t)addr_bytes[1] << 8) | 
                ((uint32_t)addr_bytes[2] << 16) | ((uint32_t)addr_bytes[3] << 24);

      // Size al (4 byte, little endian)
      if (!Buffer_ReadBytes(size_bytes, 4, 1000))
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
        break;
      }
      
      // Little endian'dan uint32_t'ye çevir
      size = (uint32_t)size_bytes[0] | ((uint32_t)size_bytes[1] << 8) | 
             ((uint32_t)size_bytes[2] << 16) | ((uint32_t)size_bytes[3] << 24);

      // Checksum hesapla
      uint32_t checksum = Bootloader_CalculateChecksum(address, size);

      if (checksum != 0xFFFFFFFF)
      {
        uint8_t response[5];
        response[0] = RESP_OK;
        // Little endian formatında gönder
        response[1] = checksum & 0xFF;
        response[2] = (checksum >> 8) & 0xFF;
        response[3] = (checksum >> 16) & 0xFF;
        response[4] = (checksum >> 24) & 0xFF;

        HAL_UART_Transmit(&huart2, response, 5, 1000);
      }
      else
      {
        uint8_t error = RESP_ERROR;
        HAL_UART_Transmit(&huart2, &error, 1, 1000);
      }
      return 1; // Continue loop
    }

    case CMD_JUMP_TO_APP:
    {
      // Önce OK yanıtı gönder
      uint8_t ok = RESP_OK;
      HAL_UART_Transmit(&huart2, &ok, 1, 1000);

      // Kısa bekle
      HAL_Delay(100);

      // Application'a atla
      Bootloader_JumpToApplication();

      // ASLA BURAYA ULAŞILMAMALI!
      // Eğer ulaşılırsa hata gönder ve loop'tan çık
      uint8_t error = RESP_ERROR;
      HAL_UART_Transmit(&huart2, &error, 1, 1000);
      return 0; // Exit loop
    }

    default:
    {
      uint8_t error = RESP_INVALID_CMD;
      HAL_UART_Transmit(&huart2, &error, 1, 1000);
      return 1; // Continue loop
    }
  }
}

/**
  * @brief Jump to user application
  */
void Bootloader_JumpToApplication(void)
{
  uint32_t app_stack_ptr = *(volatile uint32_t*)APPLICATION_START_ADDRESS;
  uint32_t app_reset_handler = *(volatile uint32_t*)(APPLICATION_START_ADDRESS + 4);

  // STM32F446 için doğru stack pointer kontrolü
  // SRAM1: 0x20000000-0x2001FFFF (128KB)
  // SRAM2: 0x20020000-0x2002FFFF (64KB) 
  // SRAM3: 0x20030000-0x2003FFFF (64KB)
  if (app_stack_ptr < 0x20000000 || app_stack_ptr > 0x20040000)
  {
    return; // Geçersiz stack pointer
  }

  // Reset handler kontrol et
  if (app_reset_handler == 0xFFFFFFFF ||
      app_reset_handler < APPLICATION_START_ADDRESS ||
      app_reset_handler >= 0x08080000)
  {
    return; // Geçersiz reset handler
  }

  // Bootloader'ı temizle
  __disable_irq();

  // SysTick'i durdur
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  // HAL'ı deinitialize et
  HAL_DeInit();

  // Tüm interrupt'ları temizle
  for (int i = 0; i < 8; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }

  // Memory barrier
  __DSB();
  __ISB();

  // Stack pointer'ı ayarla
  __set_MSP(app_stack_ptr);

  // Vector table'ı yeni konuma kaydır
  SCB->VTOR = APPLICATION_START_ADDRESS;

  // Interrupt'ları aktif et
  __enable_irq();

  // Application'a atla
  void (*app_reset)(void) = (void*)app_reset_handler;
  app_reset();
}

/**
 * @brief Erase flash memory
 */
uint8_t Bootloader_EraseFlash(uint32_t start_address, uint32_t size)
{
  FLASH_EraseInitTypeDef erase_init;
  uint32_t sector_error;

  // Güvenlik kontrolü - sadece uygulama alanını silebiliriz
  if (start_address < APPLICATION_START_ADDRESS)
  {
    return 1; // Bootloader alanını silemez
  }

  if (start_address >= 0x08080000)
  {
    return 1; // Flash sınırları dışı
  }

  // Hangi sektörü sileceğimizi belirle
  uint32_t start_sector;
  if (start_address >= 0x08008000 && start_address < 0x0800C000)
    start_sector = FLASH_SECTOR_2;
  else if (start_address >= 0x0800C000 && start_address < 0x08010000)
    start_sector = FLASH_SECTOR_3;
  else if (start_address >= 0x08010000 && start_address < 0x08020000)
    start_sector = FLASH_SECTOR_4;
  else if (start_address >= 0x08020000 && start_address < 0x08040000)
    start_sector = FLASH_SECTOR_5;
  else if (start_address >= 0x08040000 && start_address < 0x08060000)
    start_sector = FLASH_SECTOR_6;
  else if (start_address >= 0x08060000 && start_address < 0x08080000)
    start_sector = FLASH_SECTOR_7;
  else
    return 1; // Geçersiz adres

  HAL_FLASH_Unlock();

  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.Sector = start_sector;
  erase_init.NbSectors = 1; // Sadece bir sektör sil
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
  {
    HAL_FLASH_Lock();
    return 1; // Hata
  }

  HAL_FLASH_Lock();
  return 0; // Başarılı
}

/**
 * @brief Write data to flash memory (Word-aligned)
 */
uint8_t Bootloader_WriteFlash(uint32_t address, uint8_t *data, uint32_t size)
{
  // Güvenlik kontrolü - sadece uygulama alanına yazabilriz
  if (address < APPLICATION_START_ADDRESS)
  {
    return 1; // Bootloader alanına yazamaz
  }

  if (address >= 0x08080000)
  {
    return 1; // Flash sınırları dışı
  }

  if (address + size > 0x08080000)
  {
    return 1; // Flash sınırını aşıyor
  }

  if (size > 256)
  {
    return 1; // Çok büyük
  }

  HAL_FLASH_Unlock();

  // STM32F4 için word (4 byte) hizalı yazma gerekli
  uint32_t write_address = address;
  uint32_t remaining = size;
  uint32_t data_index = 0;

  while (remaining > 0) {
    if (remaining >= 4 && (write_address % 4 == 0)) {
      // 4 byte hizalı word yazma
      uint32_t word_data = ((uint32_t)data[data_index]) |
                          ((uint32_t)data[data_index + 1] << 8) |
                          ((uint32_t)data[data_index + 2] << 16) |
                          ((uint32_t)data[data_index + 3] << 24);
      
      if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, write_address, word_data) != HAL_OK) {
        HAL_FLASH_Lock();
        return 1;
      }
      write_address += 4;
      data_index += 4;
      remaining -= 4;
    } else {
      // Byte yazma (daha yavaş)
      if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, write_address, data[data_index]) != HAL_OK) {
        HAL_FLASH_Lock();
        return 1;
      }
      write_address++;
      data_index++;
      remaining--;
    }
  }

  HAL_FLASH_Lock();
  return 0; // Başarılı
}

/**
 * @brief Read data from flash memory
 */
uint8_t Bootloader_ReadFlash(uint32_t address, uint8_t *data, uint32_t size)
{
  // Güvenlik kontrolü
  if (address < BOOTLOADER_START_ADDRESS || address >= 0x08080000)
  {
    return 1; // Hata
  }

  if (size > 256)
  {
    return 1; // Çok büyük
  }

  // Flash'tan direkt okuma
  uint8_t *flash_ptr = (uint8_t*)address;
  for (uint32_t i = 0; i < size; i++)
  {
    data[i] = flash_ptr[i];
  }

  return 0; // Başarılı
}

/**
 * @brief Calculate CRC32 checksum of flash memory region
 */
uint32_t Bootloader_CalculateChecksum(uint32_t start_address, uint32_t size)
{
  uint32_t checksum = 0;
  uint8_t *flash_ptr = (uint8_t*)start_address;

  // Güvenlik kontrolü
  if (start_address < BOOTLOADER_START_ADDRESS || start_address >= 0x08080000)
  {
    return 0xFFFFFFFF; // Hata göstergesi
  }

  if (start_address + size > 0x08080000)
  {
    return 0xFFFFFFFF; // Hata göstergesi
  }

  if (size == 0)
  {
    return 0xFFFFFFFF; // Hata göstergesi
  }

  // XOR checksum (daha güvenilir)
  for (uint32_t i = 0; i < size; i++)
  {
    checksum ^= flash_ptr[i];
    checksum = (checksum << 1) | (checksum >> 31); // Rotate left
  }

  return checksum;
}

/**
 * @brief Send response byte
 */
void Bootloader_SendResponse(uint8_t response)
{
  HAL_UART_Transmit(&huart2, &response, 1, 1000);
}

/**
 * @brief Send data over UART
 */
void Bootloader_SendData(uint8_t *data, uint32_t size)
{
  HAL_UART_Transmit(&huart2, data, size, 1000);
}


// Circular buffer fonksiyonları
void Buffer_Put(CircularBuffer_t *buf, uint8_t data)
{
  buf->buffer[buf->head] = data;
  buf->head = (buf->head + 1) % UART_BUFFER_SIZE;

  if (buf->count < UART_BUFFER_SIZE) {
    buf->count++;
  } else {
    // Buffer full, overwrite oldest data
    buf->tail = (buf->tail + 1) % UART_BUFFER_SIZE;
  }
}

uint8_t Buffer_Get(CircularBuffer_t *buf, uint8_t *data)
{
  if (buf->count == 0) {
    return 0; // Buffer empty
  }

  *data = buf->buffer[buf->tail];
  buf->tail = (buf->tail + 1) % UART_BUFFER_SIZE;
  buf->count--;
  return 1; // Success
}

uint16_t Buffer_Available(CircularBuffer_t *buf)
{
  return buf->count;
}

uint8_t Buffer_Peek(CircularBuffer_t *buf, uint16_t index, uint8_t *data)
{
  if (index >= buf->count) {
    return 0; // Index out of range
  }

  uint16_t pos = (buf->tail + index) % UART_BUFFER_SIZE;
  *data = buf->buffer[pos];
  return 1; // Success
}

// UART interrupt callback
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2) {
    Buffer_Put(&uart_rx_buffer, uart_rx_byte);
    // Start next reception
    HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
