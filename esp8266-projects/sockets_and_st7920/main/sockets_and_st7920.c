#include <lwip/netdb.h>
#include <string.h>
#include <sys/param.h>

#include "connect.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "server_task.h"
#include "u8g2_esp8266_hal.h"

#define PORT CONFIG_EXAMPLE_PORT

// Globals
static const char* TAG = "main";
char               buf[256];
char               mi_ip[16];

xSemaphoreHandle bin_sem, mutex, ip_ready;

static inline void initialize_wifi(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());
}

static void setup_display(u8g2_t* u8g2)
{
    u8g2_esp8266_hal_t u8g2_esp8266_hal = {
        .mosi = GPIO_NUM_13,
        .clk = GPIO_NUM_14,
        .cs = GPIO_NUM_15
    };
    u8g2_esp8266_hal_init(u8g2_esp8266_hal);

    u8g2_Setup_st7920_s_128x64_f(u8g2, U8G2_R0,
        u8g2_esp8266_spi_byte_cb,
        u8g2_esp8266_gpio_and_delay_cb); // init u8g2 structure
    u8g2_InitDisplay(u8g2); // send init sequence to the display, display is in sleep mode after this
    u8g2_ClearDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, 0); // wake up display
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_mr);
}

static void scroll_ip(u8g2_t* u8g2)
{
    u8g2_SetFont(u8g2, u8g2_font_helvB18_tr);
    u8g2_uint_t offset = 0;
    u8g2_uint_t width = 10 + u8g2_GetStrWidth(u8g2, mi_ip);
    // we wait until an ip was assigned
    xSemaphoreTake(ip_ready, portMAX_DELAY);
    ESP_LOGW(TAG, "we got ip event dude");

    uint8_t max_laps = 3;

    // TODO: implement sort of an event to loop until there are data
    while (max_laps > 0) {
        u8g2_uint_t x;

        u8g2_FirstPage(u8g2);

        x = offset;
        u8g2_ClearBuffer(u8g2);
        do {
            u8g2_DrawStr(u8g2, x, 40, mi_ip);
            x += width;
        } while (x < u8g2_GetDisplayWidth(u8g2));

        u8g2_SendBuffer(u8g2); // warning, we still must implement a mutex for the display resource

        offset -= 1; // scroll by one pixel
        if ((u8g2_uint_t)offset < (u8g2_uint_t)-width) {
            offset = 0; // start over again
            max_laps--;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_mr);
    u8g2_ClearDisplay(u8g2);
}

// Consumer: continuously read from shared buffer
void vTaskDisplay(u8g2_t* u8g2)
{
    while (1) {
        xSemaphoreTake(bin_sem, portMAX_DELAY);
        ESP_LOGI(TAG, "there is new data");
        // Lock critical section with a mutex
        xSemaphoreTake(mutex, portMAX_DELAY);
        ESP_LOGI(TAG, "Got the lock of buffer");
        u8g2_ClearBuffer(u8g2);
        u8g2_DrawStr(u8g2, 10, 20, mi_ip);
        u8g2_DrawStr(u8g2, 0, 31, buf);
        ESP_LOGI(TAG, "Release the lock of buffer");
        xSemaphoreGive(mutex);
        // buffer already copied
        u8g2_SendBuffer(u8g2);
    }
}

void app_main()
{
    u8g2_t u8g2;
    bin_sem = xSemaphoreCreateBinary();
    mutex = xSemaphoreCreateMutex();
    ip_ready = xSemaphoreCreateBinary();

    initialize_wifi();
    tcp_server_create();
    setup_display(&u8g2);
    scroll_ip(&u8g2);
    vTaskDisplay(&u8g2);
}
