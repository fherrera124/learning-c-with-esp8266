#include "esp_http_client.h"

typedef enum {
    cmdPlay,
    cmdPause,
    cmdNext
} PlayerCmd;

extern char buffer[];
extern char access_token[];

esp_http_client_handle_t init_spotify_client();
esp_err_t                spotify_refresh_token();
esp_err_t                spotify_get_currently_playing();
void                     client_cleanup();