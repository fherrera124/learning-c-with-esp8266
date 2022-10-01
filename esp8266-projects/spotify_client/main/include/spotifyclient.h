#include "esp_http_client.h"

typedef enum {
    cmdToggle,
    cmdPrev,
    cmdNext,
} Player_cmd_t;

typedef struct {
    char                     access_token[256];
    const char*              endpoint;
    int                      status_code;
    esp_err_t                err;
    esp_http_client_method_t method;
    esp_http_client_handle_t client;
} Client_state_t;

void init_spotify_client(void);
void player_task(void* pvParameter);
void currently_playing_task(void* pvParameters);