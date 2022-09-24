#include "esp_http_client.h"

typedef enum {
    cmdPlay,
    cmdPause,
    cmdPrev,
    cmdNext
} PlayerCmd;

extern char buffer[];

esp_http_client_handle_t init_spotify_client();
void                     spotify_send_player_cmd(void* pvParameter);
void                     currently_playing(void* pvParameters);