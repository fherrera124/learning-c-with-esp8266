#include "spotifyclient.h"

#include <string.h>

#include "credentials.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"

static const char *TAG = "SPOTIFY_CLIENT";

#define MAX_HTTP_OUTPUT_BUFFER 8192

#define PLAYING      "/me/player/currently-playing"
#define PLAY         "/me/player/play"
#define PAUSE        "/me/player/pause"
#define NEXT         "/me/player/next"

#define PLAYERENDPOINT(ENDPOINT) "https://api.spotify.com/v1" ENDPOINT


char buffer[MAX_HTTP_OUTPUT_BUFFER];  // maybe I should use malloc instead and use the heap
char access_token[256];

extern const char spotify_com_root_cert_pem_start[] asm("_binary_spotify_com_root_cert_pem_start");
extern const char spotify_com_root_cert_pem_end[] asm("_binary_spotify_com_root_cert_pem_end");

esp_err_t                _http_event_handler(esp_http_client_event_t *evt);
esp_http_client_handle_t init_spotify_client();
esp_err_t                spotify_refresh_token();
esp_err_t                spotify_get_currently_playing();

static esp_http_client_config_t config = {
    .url           = "https://api.spotify.com/v1",
    .event_handler = _http_event_handler,
    .cert_pem      = spotify_com_root_cert_pem_start,
};
esp_http_client_handle_t client;

esp_http_client_handle_t init_spotify_client() {
    client = esp_http_client_init(&config);
    return client;
}

void client_cleanup() {
    esp_http_client_cleanup(client);
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static int output_len;  // Stores number of bytes read
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGW(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
        case HTTP_EVENT_HEADER_SENT:
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
            if ((output_len + evt->data_len) > MAX_HTTP_OUTPUT_BUFFER) {
                ESP_LOGE(TAG,
                         "Not enough space on buffer. "
                         "Available: %d. Incoming data: %d",
                         MAX_HTTP_OUTPUT_BUFFER - output_len, evt->data_len);
                return ESP_FAIL;
            }
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            memcpy(buffer + output_len, evt->data, evt->data_len);
            output_len += evt->data_len;
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH. Bytes read: %d", output_len);
            buffer[output_len] = 0;
            output_len         = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            int       mbedtls_err = 0;
            esp_err_t err         = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (buffer != NULL) {
                    buffer[output_len] = 0;
                    output_len         = 0;
                }
                ESP_LOGW(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGW(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

esp_err_t spotify_refresh_token() {
    esp_http_client_set_url(client, "https://accounts.spotify.com/api/token");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Authorization", "Basic " AUTH);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

    const char *post_data = "grant_type=refresh_token&refresh_token=" REFRESH;
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    /* Clear post field */
    esp_http_client_set_post_field(client, NULL, 0);
    return err;
}

esp_err_t spotify_get_currently_playing() {
    esp_http_client_set_url(client, PLAYERENDPOINT(PLAYING));
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Authorization", access_token);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_perform(client);
    return err;
}

void spotify_send_player_cmd(PlayerCmd cmd) {
    esp_http_client_method_t method;

    const char *endpoint = NULL;

    switch (cmd) {
        case cmdPlay:
            method   = HTTP_METHOD_PUT;
            endpoint = PLAYERENDPOINT(PLAY);
            break;
        case cmdPause:
            method   = HTTP_METHOD_PUT;
            endpoint = PLAYERENDPOINT(PAUSE);
            break;
        case cmdNext:
            method   = HTTP_METHOD_POST;
            endpoint = PLAYERENDPOINT(NEXT);
            break;
        default:
            return;
    }
    esp_http_client_set_url(client, endpoint);
    esp_http_client_set_method(client, method);
    esp_http_client_set_header(client, "Authorization", access_token);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_perform(client);
}