/* SPOTIFY HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "spotifyclient.h"

void encoder_init(UBaseType_t priority, QueueHandle_t **);
void wifi_init_sta(void);

static const char *TAG = "MAIN";

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    wifi_init_sta();

    /* esp32-rotary-encoder requires that the GPIO ISR service is installed */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    init_spotify_client();

    struct {
        TaskHandle_t   playing_task_hlr;
        QueueHandle_t *encoder_queue;
    } handlers = {0};

    int res = xTaskCreate(&currently_playing_task, "currently_playing_task", 4096, NULL, 5, &handlers.playing_task_hlr);
    if (res == pdPASS) {
        encoder_init(6, &handlers.encoder_queue);
        xTaskCreate(&player_task, "player_task", 4096, &handlers, 4, NULL);
    }
}
