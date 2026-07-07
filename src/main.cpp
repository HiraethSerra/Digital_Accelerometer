/*
 * Embedded Systems Project - BMA180 Accelerometer over Wi-Fi (UDP)
 * STM32F413-Discovery + onboard ISM43362 Wi-Fi module
 *
 * Merges:
 *  - BMA180 interrupt-driven reading (PA0 EXTI, I2C2)
 *  - Wi-Fi UDP streaming (X-CUBE-WIFI1 stack: wifi.c / es_wifi.c / es_wifi_io.c)
 *
 */

#include "diag/trace.h"
#include "stm32f4xx_hal.h"
#include "stm32f413h_discovery.h"
#include "es_wifi_io.h"     // declares extern wifi_data_ready_flag, http_process_flag
#include "wifi.h"
#include "wifi_conf.hpp"
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"

// ---- BMA180 Sensor Register Definitions ------------------------------------
#define BMA180_ADDR         (0x40 << 1)
#define BMA180_ACC_X_LSB    0x02
#define BMA180_CTRL_REG3    0x21       // Interrupt control register, everytime there is new movement, turn on physical INT Pin to notify SMT32.
#define BMA180_RESET_REG    0x10       // Soft reset register

//Definitions to talk to external hardware
// 0x40 << 1 because every device on I2C needs a unique address, and BMA180's is 0x40. We shift it by 1 bit because SMT32 requires the address to be formatted
//in the upper 7 bits of a byte, to leave space for read (1) or write (0) bit.

// 0x02, 0x21, 0x10 are internal registers of BMA180. Required for microcontroller to vonfigure interrupt settings or grab data

// ---- Global Handles & Flags ------------------------------------------------
I2C_HandleTypeDef hi2c2;
volatile uint8_t g_DataReadyFlag = 0;   // Set inside the merged GPIO ISR (PA0)

uint8_t g_RawBuffer[6] = {0};
int16_t x = 0, y = 0, z = 0;

static uint8_t ip_addr[4];

// ---- Prototypes -------------------------------------------------------------
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C2_Init(void);
void BMA180_Init(void);
static int  int_to_str(int val, char *buf);
static int  BuildPacket(char *buf, int buf_size, int x, int y, int z);
static bool ConnectUDP(void);

// ---- Simple int to string ("+123" / "-45") ---------------------------------
static int int_to_str(int val, char *buf) {
    char tmp[12];
    int i = 0, len = 0;
    if (val < 0) { buf[len++] = '-'; val = -val; }
    else          { buf[len++] = '+'; }
    if (val == 0) tmp[i++] = '0';
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    for (int j = i - 1; j >= 0; j--) buf[len++] = tmp[j];
    return len;
}

//Microcontroller uses bits and bytes, but to display on phone we need text, so we convert them here.
//int_to_str takes -45 and changes it to '-', '4', '5'

// ---- Build UDP packet: "X:+nnn,Y:+nnn,Z:+nnn\n" ----------------------------
static int BuildPacket(char *buf, int buf_size, int x, int y, int z) {
    char sx[12], sy[12], sz[12];
    int lx = int_to_str(x, sx); sx[lx] = '\0';
    int ly = int_to_str(y, sy); sy[ly] = '\0';
    int lz = int_to_str(z, sz); sz[lz] = '\0';

    int pos = 0;
    const char *p;
    p = "X:";  while (*p && pos < buf_size-1) buf[pos++] = *p++;
    p = sx;    while (*p && pos < buf_size-1) buf[pos++] = *p++;
    p = ",Y:"; while (*p && pos < buf_size-1) buf[pos++] = *p++;
    p = sy;    while (*p && pos < buf_size-1) buf[pos++] = *p++;
    p = ",Z:"; while (*p && pos < buf_size-1) buf[pos++] = *p++;
    p = sz;    while (*p && pos < buf_size-1) buf[pos++] = *p++;
    if (pos < buf_size-1) buf[pos++] = '\n';
    buf[pos] = '\0';
    return pos;
}

//BuildPacket glues everything together into a data package.
//It will take the static label and input string and format it. Eg: X:+024,Y:-115,Z:+980\n.

// ---- Open UDP connection to the PC/phone-facing server ---------------------
static uint8_t server_ip[4] = {SERVER_IP_1, SERVER_IP_2, SERVER_IP_3, SERVER_IP_4};

static bool ConnectUDP(void) {
    WIFI_CloseClientConnection();
    HAL_Delay(100);
    WIFI_Status_t st = WIFI_OpenClientConnection(
        0, WIFI_UDP_PROTOCOL, "BMA180",
        (char*)server_ip, SERVER_UDP_PORT, 0
    );
    return (st == WIFI_STATUS_OK);
}

// WIFI_OpenClientConnection gives socket number 0, Protocol UDP, server ip address
// ---- Main -------------------------------------------------------------------
int main(void) {
    HAL_Init(); //Wakes up internal low level drivers of STM32
    SystemClock_Config();


    trace_puts("   BMA180 ACCELEROMETER -> WI-FI UDP STREAMING    ");
    trace_puts("--------------------------------------------------");

    BSP_LED_Init(LED_GREEN);
    BSP_LED_Init(LED_RED);
    BSP_LED_Off(LED_GREEN);
    BSP_LED_Off(LED_RED);

    MX_GPIO_Init(); //Activates hardware pins, configures I2C wires (D14,D15 | PB10, PB11) and configures PA0 to watch for external interrupt.
    MX_I2C2_Init(); // Handles electrical config of I2C
    BMA180_Init(); //Sends soft reset through 0x10, checks internal ID code to verify connection and enables interrupt through 0x21

    // ---- Wi-Fi init ---------------------------------------------------------
    trace_printf("Initializing WiFi...\n");
    if (WIFI_Init() != WIFI_STATUS_OK) {
        trace_printf("ERROR: WiFi init failed\n");
        BSP_LED_On(LED_RED); while (1) {}
    }

    trace_printf("Connecting to %s...\n", WIFI_SSID);
    if (WIFI_Connect(WIFI_SSID, WIFI_PASSWORD, WIFI_ECN_WPA2_PSK) != WIFI_STATUS_OK) {
        trace_printf("ERROR: WiFi connect failed\n");
        BSP_LED_On(LED_RED); while (1) {}
    }

    WIFI_GetIP_Address(ip_addr);
    trace_printf("Board IP: %d.%d.%d.%d\n",
                 ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);

    trace_printf("Opening UDP to %d.%d.%d.%d:%d...\n",
                 SERVER_IP_1, SERVER_IP_2, SERVER_IP_3, SERVER_IP_4,
                 SERVER_UDP_PORT);

    if (!ConnectUDP()) {
        trace_printf("ERROR: UDP connect failed\n");
        BSP_LED_On(LED_RED); while (1) {}
    }

    trace_printf("Streaming. Waiting for BMA180 interrupts...\n");
    BSP_LED_On(LED_GREEN);

    uint32_t consecutive_errors = 0;

    // ---- Main loop: only send when the sensor's own interrupt fires --------
    while (1) {
        if (g_DataReadyFlag) {
            g_DataReadyFlag = 0;

            if (HAL_I2C_Mem_Read(&hi2c2, BMA180_ADDR, BMA180_ACC_X_LSB,
                                  I2C_MEMADD_SIZE_8BIT, g_RawBuffer, 6, 50) == HAL_OK) {

                x = (int16_t)((g_RawBuffer[1] << 8) | g_RawBuffer[0]) >> 2; //splits single 14-bit axis reading into two 8-bit bytes (low byte [0] and high byte [1])
                y = (int16_t)((g_RawBuffer[3] << 8) | g_RawBuffer[2]) >> 2; // << 8 shifts high byte 8 spaces left to make room for low byte
                z = (int16_t)((g_RawBuffer[5] << 8) | g_RawBuffer[4]) >> 2; // >> 2 eliminates empty space, giving full 16-bit integer

                trace_printf("BMA180 -> X: %5d | Y: %5d | Z: %5d\n", x, y, z);

                char packet[32];
                int len = BuildPacket(packet, sizeof(packet), x, y, z);

                uint16_t sent = 0;
                WIFI_Status_t status = WIFI_SendData(0, (uint8_t*)packet, (uint16_t)len, &sent);

                //If network connection drops, SMT32 instantly turns on Red LED, increments error counter and calls ConnectUDP() to rebuild connection.
                if (status != WIFI_STATUS_OK) {
                    consecutive_errors++;
                    BSP_LED_On(LED_RED);
                    trace_printf("Send error #%lu - reconnecting...\n", consecutive_errors);

                    if (ConnectUDP()) {
                        consecutive_errors = 0;
                        BSP_LED_Off(LED_RED);
                        BSP_LED_On(LED_GREEN);
                    } else {
                        HAL_Delay(500);
                    }
                } else {
                    consecutive_errors = 0;
                }
            } else {
                trace_puts("I2C Read Error during Interrupt Event.");
            }
        }
        // MCU can idle / low-power here; data-ready interrupt wakes the loop
    }
}

// ---- BMA180 Sensor Configuration --------------------------------------------
void BMA180_Init(void) {
    uint8_t check_id = 0; //store sensor's response
    uint8_t config_reg = 0; // store register values

    if (HAL_I2C_Mem_Read(&hi2c2, BMA180_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, &check_id, 1, 100) != HAL_OK || check_id == 0) {
        trace_puts("CRITICAL: BMA180 Sensor not detected on I2C2 bus!");
        while(1);
    }
    trace_printf("BMA180 Detected! Chip ID: %d\n", check_id);
    //Hal_I2C_Mem_Read reads register 0x00 from sensor, and finds chip ID

    config_reg = 0xB6;
    HAL_I2C_Mem_Write(&hi2c2, BMA180_ADDR, BMA180_RESET_REG, I2C_MEMADD_SIZE_8BIT, &config_reg, 1, 100);
    HAL_Delay(50);

    //Tells sensor to clear memory and reboot internal firmware.
    //Delay of 50 to ensure it completes a cycle

    config_reg = 0x02;
    HAL_I2C_Mem_Write(&hi2c2, BMA180_ADDR, BMA180_CTRL_REG3, I2C_MEMADD_SIZE_8BIT, &config_reg, 1, 100);
    trace_puts("BMA180 Data-Ready Hardware Interrupt Configured.");

    //Writing 0x02 into register 0x21 to configure sensor's behaviour.
}

//checks if external sensor BMA180 is still alive, reset it and configure it's interrupt

// ---- Hardware Initialization -------------------------------------------------
static void MX_I2C2_Init(void) {
    __HAL_RCC_I2C2_CLK_ENABLE(); //Powers up the clock inside STM32 for I2C block
    hi2c2.Instance = I2C2;
    hi2c2.Init.ClockSpeed = 100000;
    hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2; //1:1 balance for clock line's HIGH and LOW periods
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT; //Use standard 7bit addresses
    HAL_I2C_Init(&hi2c2);
}

//Configures internal hardware module inside STM32 that manages I2C protocols

static void MX_GPIO_Init(void) {


	//Enables internal clock paths for GPIO Port A and B
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // I2C2 Pins (SCL -> PB10, SDA -> PB11)
    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD; //Open Drain mode, pins can only pull wires to ground
    GPIO_InitStruct.Pull = GPIO_PULLUP; // PullUp resistor needed to be used with OD
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // BMA180 data-ready interrupt pin (PA0)
    GPIO_InitStruct.Pin = GPIO_PIN_0; //Connects to D2
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING; //Interrupt trigger on Rising edge
    GPIO_InitStruct.Pull = GPIO_NOPULL; // Disables internal resistors
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI0_IRQn, 2, 0); //Set priority to moderate 2
    HAL_NVIC_EnableIRQ(EXTI0_IRQn); // Enables hardware to break away from main thread on interrupt
}

//Maps physical pins

// ---- SystemClock_Config ----
static void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);

    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

// ---- Interrupt Service Routines (merged BMA180 + Wi-Fi) --------------------

extern "C" {

    // PA0 - BMA180 data-ready interrupt
    void EXTI0_IRQHandler(void) {
        HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
    }

    // PG12 - Wi-Fi module data-ready interrupt (shared EXTI15_10 line)
    void EXTI15_10_IRQHandler(void) {
        HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
    }

    // Single merged callback for both interrupt sources
    void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
        if (GPIO_Pin == GPIO_PIN_0) {
            g_DataReadyFlag = 1;
        }
        else if (GPIO_Pin == GPIO_PIN_12) {
            if (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_12) == GPIO_PIN_SET) {
                wifi_data_ready_flag = 1;
            }
        }
    }
}

#pragma GCC diagnostic pop
