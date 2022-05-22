#include <string.h>
#include <sys/param.h>
#include "u8g2_esp8266_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "connect.h"
#include "server_task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT CONFIG_EXAMPLE_PORT

// static const char *TAG = "main";

char buf[256];
char mi_ip[16];

static inline void initialize_wifi(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());
}

static void setup_display(u8g2_t *u8g2)
{
    u8g2_Setup_st7920_s_128x64_f(u8g2, U8G2_R0,
                                 u8x8_byte_esp8266_hw_spi,
                                 u8x8_gpio_and_delay_esp8266); // init u8g2 structure
    u8g2_InitDisplay(u8g2);                                    // send init sequence to the display, display is in sleep mode after this
    u8g2_ClearDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, 0); // wake up display
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_mr);
}

void app_main()
{
    u8g2_t u8g2;

    initialize_wifi();
    tcp_server_create();
    setup_display(&u8g2);

    while (1)
    {
        u8g2_ClearBuffer(&u8g2);
        u8g2_DrawStr(&u8g2, 10, 20, mi_ip);
        // strcpy(buf, u8x8_u8toa(i, 3)); /* convert m to a string with two digits */
        u8g2_DrawStr(&u8g2, 0, 31, buf);

        u8g2_SendBuffer(&u8g2);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
