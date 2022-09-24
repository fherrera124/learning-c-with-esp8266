#include "spotifyclient.h"

#include <string.h>

#include "credentials.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "parseobjects.h"
#include "rotary_encoder.h"

#define TOKEN_URL                "https://accounts.spotify.com/api/token"
#define PLAYING                  "/me/player/currently-playing"
#define PLAY                     "/me/player/play"
#define PAUSE                    "/me/player/pause"
#define PREVIOUS                 "/me/player/previous"
#define NEXT                     "/me/player/next"
#define PLAYERENDPOINT(ENDPOINT) "https://api.spotify.com/v1" ENDPOINT
#define ACQUIRE_LOCK(mux)        xSemaphoreTake(mux, portMAX_DELAY)
#define RELEASE_LOCK(mux)        xSemaphoreGive(mux)
#define MAX_HTTP_BUFFER          8192
#define MAX_RETRIES              2

/***************** GLOBALS ******************/
static const char       *TAG = "SPOTIFY_CLIENT";
char                     access_token[256];
char                     buffer[MAX_HTTP_BUFFER]; /* maybe I should malloc mem instead of using the heap */
esp_http_client_handle_t client;                  /* http client handle */
static SemaphoreHandle_t client_lock = NULL;      /* Mutex to manage access to the http client handle */
short                    retries     = 0;         /* number of retries made */
static Tokens            tokens;

static esp_err_t handleAuthReply(void);

extern const char spotify_cert_pem_start[] asm("_binary_spotify_cert_pem_start");
extern const char spotify_cert_pem_end[] asm("_binary_spotify_cert_pem_end");

#define CONFIG_CLIENT(URL, METHOD, AUTH, TYPE)                 \
    esp_http_client_set_url(client, URL);                      \
    esp_http_client_set_method(client, METHOD);                \
    esp_http_client_set_header(client, "Authorization", AUTH); \
    esp_http_client_set_header(client, "Content-Type", TYPE)

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static int output_len;  // Stores number of bytes read
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if ((output_len + evt->data_len) > MAX_HTTP_BUFFER) {
                ESP_LOGE(TAG, "Not enough space on buffer. Ignoring incoming data.");
                return ESP_FAIL;
            }
            memcpy(buffer + output_len, evt->data, evt->data_len);
            output_len += evt->data_len;
            break;
        case HTTP_EVENT_ON_FINISH:
            buffer[output_len] = 0;
            output_len         = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:;
            int       mbedtls_err = 0;
            esp_err_t err         = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (buffer != NULL) {
                    buffer[output_len] = 0;
                    output_len         = 0;
                }
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_http_client_handle_t init_spotify_client() {
    esp_http_client_config_t config = {
        .url           = "https://api.spotify.com/v1",
        .event_handler = _http_event_handler,
        .cert_pem      = spotify_cert_pem_start,
    };
    client             = esp_http_client_init(&config);
    client_lock        = xSemaphoreCreateMutex();
    tokens.accessToken = "\0";
    tokens.expiresIn   = 0;
    tokens.parsed      = 0;
    return client;
}

static esp_err_t refresh_token() {
    if ((tokens.expiresIn - 10) > time(0)) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Token expired or expiring soon. Fetching a new one.");
    CONFIG_CLIENT(TOKEN_URL, HTTP_METHOD_POST, "Basic " AUTH_TOKEN,
                  "application/x-www-form-urlencoded");

    const char *post_data = "grant_type=refresh_token&refresh_token=" REFRESH_TOKEN;
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    /* Clear post field */
    esp_http_client_set_post_field(client, NULL, 0);

    int status_code = esp_http_client_get_status_code(client);
    if (err == ESP_OK && status_code == 200) {
        handleAuthReply();
        strcpy(access_token, "Bearer ");
        strcat(access_token, tokens.accessToken);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s, status code: %d",
                 esp_err_to_name(err), status_code);
        return ESP_FAIL;
    }
    return ESP_FAIL;
}

static void trackInfoFree(TrackInfo *track) {
    free(track->name);
    /* strListClear(&track->artists);
    strListClear(&track->album); */
}

static esp_err_t handleApiReply(void) {
    TrackInfo track;
    memset(&track, 0, sizeof(TrackInfo));
    if (eTrackAllParsed == parseTrackInfo(buffer, &track)) {
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Error parsing track");
    }
    trackInfoFree(&track);
    return ESP_FAIL;
}

static esp_err_t handleAuthReply(void) {
    if (eTokensAllParsed == parseTokens(buffer, &tokens)) {
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Error parsing token");
    }
    return ESP_FAIL;
}

void currently_playing(void *pvParameters) {
    while (1) {
        ACQUIRE_LOCK(client_lock);
        refresh_token();

        CONFIG_CLIENT(PLAYERENDPOINT(PLAYING), HTTP_METHOD_GET,
                      access_token, "application/json");

    retry:;
        esp_err_t err         = esp_http_client_perform(client);
        int       status_code = esp_http_client_get_status_code(client);
        if (err == ESP_OK) {
            if (status_code == 200) {
                handleApiReply();
            } else if (status_code == 401) { /* bad token or expired */
                ESP_LOGW(TAG, "Token expired, getting a new one");
                refresh_token();
                goto retry;
            }
            retries = 0;
            ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));
            ESP_LOGD(TAG, "JSON: %s", buffer);

        } else {
            ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
            if (retries > 0 && ++retries <= 2) {
                ESP_LOGW(TAG, "Retrying %d/2...", retries);
                goto retry;
            } else {
                ESP_LOGI(TAG, "Restarting...");
                esp_restart();
            }
        }

        RELEASE_LOCK(client_lock);

        vTaskDelay(pdMS_TO_TICKS(10000)); /* Block for 10 secs. */
        /* uxTaskGetStackHighWaterMark() returns the minimum amount of remaining
         * stack space that was available to the task since the task started
         * executing - that is the amount of stack that remained unused when the
         * task stack was at its greatest (deepest) value. This is what is referred
         * to as the stack 'high water mark'.
         * */
        int watermark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGW(TAG, "CURRENTLY-PLAYING-TASK: Smallest stack ammount availability reached: %d", watermark);
    }
    vTaskDelete(NULL);
}

void spotify_send_player_cmd(void *pvParameter) {
    ESP_LOGI(TAG, "Core ID %d", xPortGetCoreID());

    int watermark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGW(TAG, "stack ammount availability on init: %d", watermark);

    PlayerCmd                cmd;
    esp_http_client_method_t method;
    const char              *endpoint = NULL;
    QueueHandle_t           *re_queue = (QueueHandle_t *)pvParameter;

    while (1) {
        rotary_encoder_event_t event = {0};

        if (pdTRUE == xQueueReceive(*re_queue, &event, pdMS_TO_TICKS(1000))) {
            cmd = event.state.direction == DIRECTION_CLOCKWISE ? cmdNext : cmdPrev;
            ESP_LOGI(TAG, "Encoder turned. Direction: %d", event.state.direction);
            switch (cmd) {
                case cmdPlay:
                    method   = HTTP_METHOD_PUT;
                    endpoint = PLAYERENDPOINT(PLAY);
                    break;
                case cmdPause:
                    method   = HTTP_METHOD_PUT;
                    endpoint = PLAYERENDPOINT(PAUSE);
                    break;
                case cmdPrev:
                    method   = HTTP_METHOD_POST;
                    endpoint = PLAYERENDPOINT(PREVIOUS);
                    break;
                case cmdNext:
                    method   = HTTP_METHOD_POST;
                    endpoint = PLAYERENDPOINT(NEXT);
                    break;
                default:
                    goto fail;
                    break;
            }

            ACQUIRE_LOCK(client_lock);

            refresh_token();

            CONFIG_CLIENT(endpoint, method, access_token, "application/json");
        retry:;
            esp_err_t err = esp_http_client_perform(client);

            if (err == ESP_OK) {
                retries = 0;
                ESP_LOGI(TAG, "HTTP Status Code = %d, content_length = %d",
                         esp_http_client_get_status_code(client),
                         esp_http_client_get_content_length(client));
            } else {
                ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
                if (retries > 0 && ++retries <= 2) {
                    ESP_LOGW(TAG, "Retrying %d/2...", retries);
                    goto retry;
                } else {
                    ESP_LOGI(TAG, "Restarting...");
                    esp_restart();
                }
            }

            RELEASE_LOCK(client_lock);

            /* For the actual use case of this encoder, I'm using it as a set of
             * buttons, only interested for the first shift (left or rigth). So when
             * first detected the first rotation, we put the task in suspention, while
             * the ISR is overwriting the 1 length queue in the lapse of your fingers
             * rotating the encoder. When the task is active again, the task reset
             * the queue with the value of the last move of the encoder */
            vTaskDelay(pdMS_TO_TICKS(500));
            xQueueReset(*re_queue);
            /* canary variable to monitor the task stack */
            watermark = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGW(TAG, "PLAYER-COMMAND-TASK: Smallest stack ammount availability reached: %d", watermark);
        }
    }
fail:
    ESP_LOGE(TAG, "queue receive failed");

    // ESP_ERROR_CHECK(rotary_encoder_uninit(&info)); FIX
    vTaskDelete(NULL);
}
