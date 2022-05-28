#include <stdint.h>
#include "u8g2_esp8266_hal.h"
#include "rom/ets_sys.h"
#include <string.h>

static const gpio_config_t gpio_c = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask = GPIO_OUTPUT_MASK,
    .pull_down_en = 0,
    .pull_up_en = 0};

static inline void init_config()
{
    // Init spi configuration
    spi_config_t spi_config;
    memset(&spi_config, 0, sizeof(spi_config));
    spi_config.interface.val = SPI_DEFAULT_INTERFACE;
    spi_config.intr_enable.val = SPI_MASTER_DEFAULT_INTR_ENABLE;
    spi_config.interface.cs_en = CANCEL_HARDWARE_CS;
    spi_config.interface.miso_en = DISABLE_SPI_MISO;
    spi_config.interface.cpol = SPI_CPOL_HIGH;
    spi_config.interface.cpha = SPI_CPHA_HIGH;
    spi_config.mode = SPI_MASTER_MODE;
    spi_config.clk_div = SPI_2MHz_DIV;
    spi_config.event_cb = NULL;
    spi_config.interface.bit_tx_order = SPI_BIT_ORDER_LSB_FIRST;
    spi_config.interface.byte_tx_order = SPI_BYTE_ORDER_MSB_FIRST;

    spi_init(HSPI_HOST, &spi_config);
}

uint8_t u8x8_gpio_and_delay_esp8266(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:; // called once during init phase of u8g2/u8x8
        gpio_config(&gpio_c);
        break;

    case U8X8_MSG_DELAY_MILLI:
        ets_delay_us(1000 * arg_int);
        // vTaskDelay(arg_int / portTICK_PERIOD_MS);
        break;

    case U8X8_MSG_DELAY_NANO:
        // vTaskDelay(arg_int == 0 ? 0 : arg_int / portTICK_PERIOD_MS);
        os_delay_us(arg_int == 0 ? 0 : 1);
        break;

    case U8X8_MSG_GPIO_CS: // CS (chip select) pin: Output level in arg_int
        gpio_set_level(U8G2_ESP8266_CS_GPIO, arg_int);
        break;
    }
    return 1;
}

uint8_t u8x8_byte_esp8266_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
    case U8X8_MSG_BYTE_INIT:
        /* disable chipselect */
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
        init_config();
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->post_chip_enable_wait_ns, NULL);
        break;

    case U8X8_MSG_BYTE_END_TRANSFER:
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->pre_chip_disable_wait_ns, NULL);
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
        break;

    case U8X8_MSG_BYTE_SEND:;
        uint8_t *data = (uint8_t *)arg_ptr;
        uint32_t buf;

        while (arg_int > 0)
        {
            spi_trans_t trans = {0};
            trans.mosi = &buf;
            trans.bits.mosi = 8;
            buf = *data << 24;
            spi_trans(HSPI_HOST, &trans);
            data++;
            arg_int--;
        }
        break;
    }
    return 1;
}
