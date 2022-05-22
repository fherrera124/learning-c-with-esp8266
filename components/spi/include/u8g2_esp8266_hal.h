#include "u8g2.h"
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi.h"
#include "FreeRTOS.h"
#include "freertos/task.h"

// HW SCK pin.
#define U8G2_ESP8266_D0_GPIO 14
// HW MOSI pin.
#define U8G2_ESP8266_D1_GPIO 13
#define U8G2_ESP8266_CS_GPIO 15

#define GPIO_OUTPUT_MASK ((1ULL << U8G2_ESP8266_D0_GPIO) | (1ULL << U8G2_ESP8266_CS_GPIO))

uint8_t u8x8_gpio_and_delay_esp8266(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_byte_esp8266_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#define CANCEL_HARDWARE_CS 0
#define DISABLE_SPI_MISO 0
