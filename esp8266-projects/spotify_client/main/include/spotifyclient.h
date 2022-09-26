#include "esp_http_client.h"

typedef enum {
    cmdToggle,
    cmdPrev,
    cmdNext,
} PlayerCmd;

esp_http_client_handle_t init_spotify_client(void);
void                     player_task(void* pvParameter);
void                     currently_playing_task(void* pvParameters);