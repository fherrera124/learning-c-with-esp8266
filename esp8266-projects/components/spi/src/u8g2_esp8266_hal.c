#include "u8g2_esp8266_hal.h"

#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi.h"
#include "esp8266/spi_struct.h"
#include "freertos/portmacro.h"
#include "rom/ets_sys.h"

#define GPIO_MOSI            GPIO_NUM_13
#define GPIO_SCLK            GPIO_NUM_14
#define GPIO_CS              GPIO_NUM_15
#define GPIO_OUTPUT_MASK     ((1ULL << GPIO_MOSI) | (1ULL << GPIO_SCLK))
#define SPI_CUSTOM_INTERFACE 0x43;  // spi_interface_t bit fields: mosi_en, byte_tx_order, cpha, cpol

static const gpio_config_t gpio_c = {
    .intr_type    = GPIO_INTR_DISABLE,
    .mode         = GPIO_MODE_OUTPUT,
    .pin_bit_mask = GPIO_OUTPUT_MASK,
    .pull_down_en = 0,
    .pull_up_en   = 0};

static inline void init_config() {
    // Init spi configuration
    spi_config_t spi_config;
    memset(&spi_config, 0, sizeof(spi_config));
    spi_config.interface.val   = SPI_CUSTOM_INTERFACE;
    spi_config.intr_enable.val = SPI_MASTER_DEFAULT_INTR_ENABLE;
    spi_config.mode            = SPI_MASTER_MODE;
    spi_config.clk_div         = SPI_2MHz_DIV;
    spi_config.event_cb        = NULL;

    spi_init(HSPI_HOST, &spi_config);
}

uint8_t u8x8_gpio_and_delay_esp8266(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,
                                    void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:  // called once during init phase
            gpio_config(&gpio_c);
            break;

        case U8X8_MSG_DELAY_MILLI:
            os_delay_us(1000 * arg_int);
            break;

        case U8X8_MSG_GPIO_CS:  // CS (chip select) pin: Output level in arg_int
            gpio_set_level(GPIO_CS, arg_int);
            break;
    }
    return 1;
}

uint8_t u8x8_byte_esp8266_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,
                                 void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
            init_config();
            break;

        case U8X8_MSG_BYTE_SEND:;
            uint8_t *data = (uint8_t *)arg_ptr;

            while (arg_int-- > 0) {
                
                // Waiting for an incomplete transfer
                while ((&SPI1)->cmd.usr);

                portENTER_CRITICAL();

                (&SPI1)->user.usr_command      = 0;  // Discard cmd
                (&SPI1)->user.usr_addr         = 0;  // Discard addr
                (&SPI1)->user.usr_miso         = 0;  // Discard miso
                (&SPI1)->user.usr_mosi         = 1;  // Enable mosi
                (&SPI1)->user1.usr_mosi_bitlen = 8 - 1;
                (&SPI1)->data_buf[0]           = *data++;

                (&SPI1)->cmd.usr = 1;  // Start transmission

                portEXIT_CRITICAL();
            }
            break;
    }
    return 1;
}
