/* SPOTIFY HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
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

static const char *TAG = "MAIN";

esp_http_client_handle_t client;

static void trackInfoFree(TrackInfo *track) {
    free(track->name);
    /* strListClear(&track->artists);
    strListClear(&track->album); */
}

static void handleApiReply(void) {
    TrackInfo track;
    memset(&track, 0, sizeof(TrackInfo));
    if (parseTrackInfo(buffer, &track) == eTrackAllParsed) {
        ;
    } else {
        ESP_LOGE(TAG, "Error parsing track");
    }
    trackInfoFree(&track);
}

static void refresh_token(void) {
    esp_err_t err = spotify_refresh_token();

    int status_code = esp_http_client_get_status_code(client);
    if (err == ESP_OK && status_code == 200) {
        cJSON *root = cJSON_Parse(buffer);
        if (root == NULL) {
            ESP_LOGE(TAG, "Error parsing token");
            return;
        }
        strcpy(access_token, "Bearer ");
        strcat(access_token, cJSON_GetObjectItem(root, "access_token")->valuestring);
        cJSON_Delete(root);
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s, status code: %d",
                 esp_err_to_name(err), status_code);
    }
}

static void currently_playing(void *pvParameters) {
    while (1) {
        refresh_token();

        esp_err_t err = spotify_get_currently_playing();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));
            ESP_LOGD(TAG, "JSON: %s", buffer);
            handleApiReply();

        } else {
            ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
            ESP_LOGD(TAG, "Available heap: %d", esp_get_free_heap_size());
        }

        /* Block for 100000ms (10 secs). */
        const TickType_t xDelay = 100000 / portTICK_PERIOD_MS;
        vTaskDelay(pdMS_TO_TICKS(xDelay));
    }
    vTaskDelete(NULL);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(TAG, "Connected to AP, begin http example");

    /* esp32-rotary-encoder requires that the GPIO ISR service is installed */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    initPaths();

    client = init_spotify_client();

    xTaskCreate(&currently_playing, "currently_playing", 8192, NULL, 5, NULL);

    QueueHandle_t *encoder_queue_ptr = NULL;
    encoder_init(6, &encoder_queue_ptr);
}
