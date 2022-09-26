#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

// define three event flag bit variable
#define BIT_0 (1UL << 0)
#define BIT_1 (1UL << 1)
#define BIT_2 (1UL << 2)

// define the size in bytes of the stack of a task
#define STACK_SIZE 1024

static const char *OUT1 = "Output task 1";
static const char *OUT2 = "Output task 2";

// declare event group handler variable
EventGroupHandle_t xEventGroup;

// definition of tasks

void Task1(void *pvParameters)
{
    while (1)
    {
        // set flag bit BIT_0
        xEventGroupSetBits(xEventGroup, BIT_0);
        // delay this task 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Task2(void *pvParameters)
{
    while (1)
    {
        // set flag bit BIT_1
        xEventGroupSetBits(xEventGroup, BIT_1);
        // delay this task 150ms
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void Task3(void *pvParameters)
{
    while (1)
    {
        // set flag bit BIT_2
        xEventGroupSetBits(xEventGroup, BIT_2);
        // delay this task 120ms
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

uint8_t count = 0;

// Only wait for bits of task1 and task3
void OutputTask1(void *pvParameters)
{
    EventBits_t xEventGroupValue;
    const EventBits_t xBitsToWaitFor = (BIT_0 | BIT_2); // 5

    while (1)
    {
        xEventGroupValue = xEventGroupWaitBits(
            xEventGroup,    // The event group being tested.
            xBitsToWaitFor, // The bits within the event group to wait for.
            pdTRUE,         // Bits should be cleared before returning.
            pdFALSE,        // Don't wait for all bits, any bit will do.
            portMAX_DELAY); // Maximum time to wait for any bit to be set

        if ((xEventGroupValue & xBitsToWaitFor) == xBitsToWaitFor)
            ESP_LOGW(OUT1, "Both Event1 and Event3 occurred");
        else if ((xEventGroupValue & BIT_0) != 0)
            ESP_LOGI(OUT1, "Event1 occurred");
        else if ((xEventGroupValue & BIT_2) != 0)
            ESP_LOGI(OUT1, "Event3 occurred");
        else
            ESP_LOGE(OUT1, "Timeout");

        if (++count == 5)
        {
            count = 0;
            // with this delay, we can catch both event1 and event3
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

// Only wait for the bit of task2
void OutputTask2(void *pvParameters)
{
    EventBits_t xEventGroupValue;
    const EventBits_t xBitToWaitFor = BIT_1; // 2

    while (1)
    {
        xEventGroupValue = xEventGroupWaitBits(
            xEventGroup,    // The event group being tested.
            xBitToWaitFor,  // The bits within the event group to wait for.
            pdTRUE,         // Bits should be cleared before returning.
            pdFALSE,        // Don't wait for all bits, any bit will do.
            portMAX_DELAY); // Maximum time to wait for any bit to be set

        if ((xEventGroupValue & xBitToWaitFor) == xBitToWaitFor)
            ESP_LOGI(OUT2, "Event2 occurred");
        else
            ESP_LOGE(OUT2, "Timeout");
    }
}

void app_main()
{
    // create event group
    xEventGroup = xEventGroupCreate();
    // create three tasks
    xTaskCreate(Task1, "Task 1", STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(Task2, "Task 2", STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(Task3, "Task 3", STACK_SIZE, NULL, 1, NULL);
    // create tasks that output the actions of the others tasks
    xTaskCreate(OutputTask1, "Output Task 1", STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(OutputTask2, "Output Task 2", STACK_SIZE, NULL, 1, NULL);
}
