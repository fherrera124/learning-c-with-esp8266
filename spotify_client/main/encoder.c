#include <stdbool.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rotary_encoder.h"
#include "spotifyclient.h"

#define TAG "ROTARY ENCODER"

#define ROT_ENC_A_GPIO   CONFIG_RE_CLK_GPIO
#define ROT_ENC_B_GPIO   CONFIG_RE_DT_GPIO
#define ROT_ENC_BTN_GPIO CONFIG_RE_BTN_GPIO

#define ENABLE_HALF_STEPS false  // Set to true to enable tracking of rotary encoder at half step resolution
#define RESET_AT          0      // Set to a positive non-zero number to reset the position if this value is exceeded
#define FLIP_DIRECTION    false  // Set to true to reverse the clockwise/counterclockwise sense

rotary_encoder_info_t info = {0};

QueueHandle_t event_queue;

static void rotary_task(void* pvParameter) {
    ESP_LOGI(TAG, "Core ID %d", xPortGetCoreID());

    while (1) {
        // Wait for incoming events on the event queue.
        rotary_encoder_event_t event = {0};
        if (xQueueReceive(event_queue, &event, portMAX_DELAY)) {
            //cmd = event.state.direction == DIRECTION_CLOCKWISE ? cmdNext : cmdPrev;

            //send_player_cmd(cmd);

            /* For the actual use case of this encoder, I'm using it as a set of
             * buttons, only interested for the first shift (left or rigth). So when
             * first detected the first rotation, we put the task in suspention, while
             * the ISR is overwriting the 1 length queue in the lapse your fingers
             * keep rotating the encoder. When the task is active again, the task
             * has the last value from the last burst, thus clearing it. */
            vTaskDelay(pdMS_TO_TICKS(500));  // when first detected the first rotation
            xQueueReset(event_queue);
        }
    }
    ESP_LOGE(TAG, "queue receive failed");

    ESP_ERROR_CHECK(rotary_encoder_uninit(&info));
    vTaskDelete(NULL);
}

void encoder_init(UBaseType_t priority, QueueHandle_t** queue) {
    // IMPORTANT: GPIO ISR service should already be installed

    // Initialise the rotary encoder device with the GPIOs for A and B signals
    ESP_ERROR_CHECK(rotary_encoder_init(&info, ROT_ENC_A_GPIO, ROT_ENC_B_GPIO, ROT_ENC_BTN_GPIO));
    ESP_ERROR_CHECK(rotary_encoder_enable_half_steps(&info, ENABLE_HALF_STEPS));
#ifdef FLIP_DIRECTION
    ESP_ERROR_CHECK(rotary_encoder_flip_direction(&info));
#endif

    // Create a queue for events from the rotary encoder driver.
    // Tasks can read from this queue to receive up to date position information.
    event_queue = rotary_encoder_create_queue();
    ESP_ERROR_CHECK(rotary_encoder_set_queue(&info, event_queue));

    *queue = &event_queue;
}
