#include "spotifyclient.h"

#include <string.h>

#include "credentials.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "parseobjects.h"
#include "rotary_encoder.h"

#define PLAYER                   "/me/player"
#define TOKEN_URL                "https://accounts.spotify.com/api/token"
#define PLAYING                  PLAYER "?market=AR&additional_types=episode"
#define PLAY                     PLAYER "/play"
#define PAUSE                    PLAYER "/pause"
#define PREVIOUS                 PLAYER "/previous"
#define NEXT                     PLAYER "/next"
#define PLAYERENDPOINT(ENDPOINT) "https://api.spotify.com/v1" ENDPOINT
#define ACQUIRE_LOCK(mux)        xSemaphoreTake(mux, portMAX_DELAY)
#define RELEASE_LOCK(mux)        xSemaphoreGive(mux)
#define MAX_HTTP_BUFFER          8192
#define MAX_RETRIES              2

/***************** GLOBALS ******************/
static const char       *TAG = "SPOTIFY_CLIENT";
char                     buffer[MAX_HTTP_BUFFER]; /* maybe I should malloc mem instead of using the heap */
static SemaphoreHandle_t client_lock  = NULL;     /* Mutex to manage access to the http client handle */
short                    retries      = 0;        /* number of retries on error connections */
static Tokens            tokens       = {.access_token = ""};
TrackInfo                cur_track    = {.name = ""};
TrackInfo               *curr_trk_ptr = &cur_track;
Client_state_t           state        = {0};

static const char *HTTP_METHOD_MAPPING[] = {
    "GET",
    "POST",
    "PUT",
};
/********************************************/

/******** PRIVATE FUNCTION PROTOTYPES *******/
static esp_err_t validate_token();
static void      get_active_devices(StrList *);
static void      trackInfoFree(TrackInfo *track);
static void      handle_200_response(TrackInfo **new_track);
static esp_err_t _http_event_handler(esp_http_client_event_t *evt);
/******** PRIVATE FUNCTION PROTOTYPES *******/

extern const char spotify_cert_pem_start[] asm("_binary_spotify_cert_pem_start");
extern const char spotify_cert_pem_end[] asm("_binary_spotify_cert_pem_end");

#define PREPARE_CLIENT(state, AUTH, TYPE)                            \
    esp_http_client_set_url(state.client, state.endpoint);           \
    esp_http_client_set_method(state.client, state.method);          \
    esp_http_client_set_header(state.client, "Authorization", AUTH); \
    esp_http_client_set_header(state.client, "Content-Type", TYPE)

/*
   "204" on "GET /me/player" means the actual device is inactive
   "204" on "PUT /me/player" means playback sucessfuly transfered to an active device
*/
#define DEVICE_INACTIVE(state) (                        \
    !strcmp(state.endpoint, PLAYERENDPOINT(PLAYING)) && \
    state.method == HTTP_METHOD_GET &&                  \
    state.status_code == 204)

#define PLAYBACK_TRANSFERED(state) (                   \
    !strcmp(state.endpoint, PLAYERENDPOINT(PLAYER)) && \
    state.method == HTTP_METHOD_PUT &&                 \
    (state.status_code == 204 || state.status_code == 202))  // on my sangean it returns 202 instead of 204

#define SWAP_POINTERS(pt1, pt2) \
    TrackInfo *temp = pt1;      \
    pt1             = pt2;      \
    pt2             = temp

void init_spotify_client() {
    init_functions_cb();

    esp_http_client_config_t config = {
        .url           = "https://api.spotify.com/v1",
        .event_handler = _http_event_handler,
        .cert_pem      = spotify_cert_pem_start,
    };
    state.client = esp_http_client_init(&config);
    client_lock  = xSemaphoreCreateMutex();
}

void currently_playing_task(void *pvParameters) {
    bool on_try_current_dev = false;

    TrackInfo  track       = {.name = ""};
    TrackInfo *new_trk_ptr = &track;

    while (1) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000))) {
            vTaskDelay(pdMS_TO_TICKS(500));  // wait for the server to update the current track
        }
        ACQUIRE_LOCK(client_lock);
        state.method   = HTTP_METHOD_GET;
        state.endpoint = PLAYERENDPOINT(PLAYING);

    prepare:
        validate_token();
        PREPARE_CLIENT(state, state.access_token, "application/json");

    retry:;
        state.err         = esp_http_client_perform(state.client);
        state.status_code = esp_http_client_get_status_code(state.client);
        esp_http_client_set_post_field(state.client, NULL, 0); /* Clear post field */
        if (state.err == ESP_OK) {
            ESP_LOGD(TAG, "Received:\n%s", buffer);
            if (state.status_code == 200) {
                retries = 0;
                handle_200_response(&new_trk_ptr);
                goto sleep;
            }
            if (state.status_code == 401) { /* bad token or expired */
                ESP_LOGW(TAG, "Token expired, getting a new one");
                goto prepare;
            }
            if (DEVICE_INACTIVE(state)) { /* Playback not available or active */

                char buf[100];

                if (!on_try_current_dev && curr_trk_ptr->device.id) {
                    on_try_current_dev = true;
                    snprintf(buf, 100, "{\"device_ids\":[\"%s\"],\"play\":%s}", curr_trk_ptr->device.id, "true");
                    ESP_LOGW(TAG, "The playblack will be transfered to device: %s", curr_trk_ptr->device.id);
                } else {  // already tried with current device
                    ESP_LOGW(TAG, "Failed connecting with last device");
                    StrList devices;
                    get_active_devices(&devices);

                    StrListItem *dev = devices.first;  // for now we pick the first device on the list
                    if (dev) {
                        snprintf(buf, 100, "{\"device_ids\":[\"%s\"],\"play\":%s}", dev->str, "true");
                    } else {
                        ESP_LOGE(TAG, "No devices found :c");
                        goto sleep;
                    }
                    /* while (dev) {
                        ESP_LOGI(TAG, "Device id: %s", dev->str);
                        // choose the device according to any preference
                        dev = dev->next;
                    } */
                    strListClear(&devices);
                }

                state.endpoint = PLAYERENDPOINT(PLAYER);
                state.method   = HTTP_METHOD_PUT;
                esp_http_client_set_post_field(state.client, buf, strlen(buf));

                goto prepare;
            }
            if (PLAYBACK_TRANSFERED(state)) {
                ESP_LOGI(TAG, "Playback transfered to: %s", curr_trk_ptr->device.id);
                on_try_current_dev = false;
                goto sleep;
            }
            /* Unhandled status_code follows */
            ESP_LOGE(TAG, "ENDPOINT: %s, METHOD: %s, STATUS_CODE: %d",
                     state.endpoint, HTTP_METHOD_MAPPING[state.method], state.status_code);
            if (*buffer) {
                ESP_LOGE(TAG, buffer);
            }
            goto sleep;

        } else {
            ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(state.err));
            if (retries > 0 && ++retries <= 2) {
                ESP_LOGW(TAG, "Retrying %d/2...", retries);
                goto retry;
            } else {
                ESP_LOGI(TAG, "Restarting...");
                esp_restart();
            }
        }
    sleep:
        RELEASE_LOCK(client_lock);
        /* Block for 10 secs. */
        // vTaskDelay(pdMS_TO_TICKS(10000));
        /* uxTaskGetStackHighWaterMark() returns the minimum amount of remaining
         * stack space that was available to the task since the task started
         * executing - that is the amount of stack that remained unused when the
         * task stack was at its greatest (deepest) value. This is what is referred
         * to as the stack 'high water mark'.
         * */
        ESP_LOGD(TAG, "[CURRENTLY_PLAYING]: uxTaskGetStackHighWaterMark(): %d",
                 uxTaskGetStackHighWaterMark(NULL));
        ESP_LOGD(TAG, "[CURRENTLY_PLAYING]: esp_get_minimum_free_heap_size(): %d",
                 esp_get_minimum_free_heap_size());
        ESP_LOGD(TAG, "[CURRENTLY_PLAYING]: esp_get_free_heap_size(): %d",
                 esp_get_free_heap_size());
    }
    vTaskDelete(NULL);
}

void player_task(void *pvParameter) {
    Player_cmd_t cmd;

    struct {
        TaskHandle_t   playing_task_hlr;
        QueueHandle_t *encoder_queue;
    } *hdls = pvParameter;

    QueueHandle_t *re_queue = (QueueHandle_t *)hdls->encoder_queue;

    TaskHandle_t task_hdr = (TaskHandle_t)hdls->playing_task_hlr;

    while (1) {
        rotary_encoder_event_t event = {0};

        if (pdTRUE == xQueueReceive(*re_queue, &event, pdMS_TO_TICKS(1000))) {
            if (event.state.btn_pushed == true) {
                cmd = cmdToggle;
                ESP_LOGD(TAG, "Button pushed.");
            } else {
                cmd = event.state.direction == DIRECTION_CLOCKWISE ? cmdNext : cmdPrev;
                ESP_LOGD(TAG, "Encoder turned. Direction: %d", event.state.direction);
            }
            switch (cmd) {
                case cmdToggle:
                    state.method   = HTTP_METHOD_PUT;
                    state.endpoint = curr_trk_ptr->isPlaying ? PLAYERENDPOINT(PAUSE) : PLAYERENDPOINT(PLAY);
                    break;
                case cmdPrev:
                    state.method   = HTTP_METHOD_POST;
                    state.endpoint = PLAYERENDPOINT(PREVIOUS);
                    break;
                case cmdNext:
                    state.method   = HTTP_METHOD_POST;
                    state.endpoint = PLAYERENDPOINT(NEXT);
                    break;
                default:
                    goto fail;
                    break;
            }

            ACQUIRE_LOCK(client_lock);

            validate_token();

            PREPARE_CLIENT(state, state.access_token, "application/json");
        retry:
            ESP_LOGD(TAG, "Endpoint to send: %s", state.endpoint);
            state.err         = esp_http_client_perform(state.client);
            state.status_code = esp_http_client_get_status_code(state.client);
            int length        = esp_http_client_get_content_length(state.client);

            if (state.err == ESP_OK) {
                retries = 0;
                ESP_LOGD(TAG, "HTTP Status Code = %d, content_length = %d",
                         state.status_code, length);
                if (cmd == cmdToggle) {
                    /* If for any reason, we dont have the actual state
                     * of the player, then when sending play command when
                     * paused, or viceversa, we receive error 403. */
                    if (state.status_code == 403) {
                        if (strcmp(state.endpoint, PLAYERENDPOINT(PLAY)) == 0) {
                            state.endpoint = PLAYERENDPOINT(PAUSE);
                        } else {
                            state.endpoint = PLAYERENDPOINT(PLAY);
                        }
                        esp_http_client_set_url(state.client, state.endpoint);
                        goto retry;  // add max number of retries maybe
                    } else {         /* all ok?? */
                        curr_trk_ptr->isPlaying = !curr_trk_ptr->isPlaying;
                    }
                } else {
                    /* The command was prev or next, so we notify to the currently_playing_task
                     * to cut the 10000 ms delay and fetch the new track info. TODO: only notify
                     * if the status code of prev/next is 200 like */
                    xTaskNotifyGive(task_hdr);
                }
            } else {
                ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(state.err));
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

            ESP_LOGD(TAG, "[PLAYER-TASK]: uxTaskGetStackHighWaterMark(): %d",
                     uxTaskGetStackHighWaterMark(NULL));
        }
    }
fail:
    ESP_LOGE(TAG, "queue receive failed");

    // ESP_ERROR_CHECK(rotary_encoder_uninit(&info)); FIX
    vTaskDelete(NULL);
}

static void get_active_devices(StrList *devices) {
    state.endpoint = PLAYERENDPOINT(PLAYER "/devices");
    state.method   = HTTP_METHOD_GET;
    PREPARE_CLIENT(state, state.access_token, "application/json");

    state.err         = esp_http_client_perform(state.client);
    state.status_code = esp_http_client_get_status_code(state.client);
    esp_http_client_set_post_field(state.client, NULL, 0);

    ESP_LOGW(TAG, "Active devices:\n%s", buffer);

    //*devices = malloc(sizeof(*devices));
    available_devices(buffer, devices);
}

static void trackInfoFree(TrackInfo *track) {
    free(track->name);
    strListClear(&(track->artists));
    free(track->album);
    free(track->device.id);
    free(track->device.name);
}

static void handle_200_response(TrackInfo **new_track) {
    int val = parseTrackInfo(buffer, *new_track);

    if (eTrackAllParsed != val) {
        ESP_LOGE(TAG, "Error parsing track. Flags parsed: %x", val);
    } else {
        if (strcmp(curr_trk_ptr->name, (*new_track)->name) != 0) {
            trackInfoFree(curr_trk_ptr);
            SWAP_POINTERS(*new_track, curr_trk_ptr);
            ESP_LOGI(TAG, "***** New Track *****");
            ESP_LOGI(TAG, "Title: %s", curr_trk_ptr->name);
            StrListItem *artist = curr_trk_ptr->artists.first;
            while (artist) {
                ESP_LOGI(TAG, "Artist: %s", artist->str);
                artist = artist->next;
            }
            ESP_LOGI(TAG, "Album: %s", curr_trk_ptr->album);
        } else {
            trackInfoFree(*new_track);
        }
    }
}

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

static esp_err_t validate_token() {
    if ((tokens.expiresIn - 10) > time(0)) {
        return ESP_OK;
    }
    free(tokens.access_token);
    ESP_LOGW(TAG, "Access Token expired or expiring soon. Fetching a new one.");

    state.endpoint = TOKEN_URL;
    state.method   = HTTP_METHOD_POST;
    PREPARE_CLIENT(state, "Basic " AUTH_TOKEN, "application/x-www-form-urlencoded");

    const char *post_data = "grant_type=refresh_token&refresh_token=" REFRESH_TOKEN;
    esp_http_client_set_post_field(state.client, post_data, strlen(post_data));
    state.err         = esp_http_client_perform(state.client);
    state.status_code = esp_http_client_get_status_code(state.client);
    esp_http_client_set_post_field(state.client, NULL, 0); /* Clear post field */

    if (state.err == ESP_OK && state.status_code == 200) {
        if (eTokensAllParsed == parseTokens(buffer, &tokens)) {
            strcpy(state.access_token, "Bearer ");
            strcat(state.access_token, tokens.access_token);
            ESP_LOGW(TAG, "Access Token obtained:\n%s", tokens.access_token);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Error trying parse token from:\n%s", buffer);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s, status code: %d",
                 esp_err_to_name(state.err), state.status_code);
        ESP_LOGE(TAG, "The answer was:\n%s", buffer);
        return ESP_FAIL;
    }
    return ESP_FAIL;
}