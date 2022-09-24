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
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "parseobjects.h"
#include "protocol_examples_common.h"
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

    initPaths();

    init_spotify_client();

    xTaskCreate(&currently_playing, "currently_playing", 4096, NULL, 5, NULL);

    QueueHandle_t *encoder_queue_ptr = NULL;
    encoder_init(6, &encoder_queue_ptr);

    xTaskCreate(&spotify_send_player_cmd, "spotify_send_player_cmd", 4096, encoder_queue_ptr, 4, NULL);
}
