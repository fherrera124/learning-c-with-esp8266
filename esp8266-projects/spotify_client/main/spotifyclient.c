/* Includes ------------------------------------------------------------------*/
#include "spotifyclient.h"

#include <string.h>

#include "buffer_callbacks.h"
#include "credentials.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "parseobjects.h"
#include "rotary_encoder.h"
#include "semphr.h"

/* Private macro -------------------------------------------------------------*/
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
#define RETRIES_ERR_CONN         2

/* Private function prototypes -----------------------------------------------*/
static esp_err_t _validate_token();
static void      _get_active_devices(StrList *);
static void      _track_info_free(TrackInfo *track);
static void      _handle_200_response(TrackInfo **new_track);
static void      _handle_err_connection();
static esp_err_t _http_event_handler(esp_http_client_event_t *evt);
static void      currently_playing_task(void *pvParameters);

/* Private variables ---------------------------------------------------------*/
static const char       *TAG = "SPOTIFY_CLIENT";
char                     buffer[MAX_HTTP_BUFFER]; /* maybe I should malloc mem instead of using the heap */
static SemaphoreHandle_t client_lock  = NULL;     /* Mutex to manage access to the http client handle */
short                    retries      = 0;        /* number of retries on error connections */
static Tokens            tokens       = {.access_token = ""};
TrackInfo                cur_track    = {.name = ""};
TrackInfo               *curr_trk_ptr = &cur_track;
Client_state_t           state        = {0};
QueueHandle_t            playing_queue_hlr;
TaskHandle_t             playing_task_hlr;  // for notify the task
QueueHandle_t           *encoder_queue;

static const char *HTTP_METHOD_LOOKUP[] = {
    "GET",
    "POST",
    "PUT",
};

extern const char spotify_cert_pem_start[] asm("_binary_spotify_cert_pem_start");
extern const char spotify_cert_pem_end[] asm("_binary_spotify_cert_pem_end");

#define PREPARE_CLIENT(state, AUTH, TYPE)                            \
    esp_http_client_set_url(state.client, state.endpoint);           \
    esp_http_client_set_method(state.client, state.method);          \
    esp_http_client_set_header(state.client, "Authorization", AUTH); \
    esp_http_client_set_header(state.client, "Content-Type", TYPE)

/* -"204" on "GET /me/player" means the actual device is inactive
 * -"204" on "PUT /me/player" means playback sucessfuly transfered
 *   to an active device (although my Sangean returns 202) */
#define DEVICE_INACTIVE(state) (                        \
    !strcmp(state.endpoint, PLAYERENDPOINT(PLAYING)) && \
    state.method == HTTP_METHOD_GET &&                  \
    state.status_code == 204)

#define PLAYBACK_TRANSFERED(state) (                   \
    !strcmp(state.endpoint, PLAYERENDPOINT(PLAYER)) && \
    state.method == HTTP_METHOD_PUT &&                 \
    (state.status_code == 204 || state.status_code == 202))

#define SWAP_POINTERS(pt1, pt2) \
    TrackInfo *temp = pt1;      \
    pt1             = pt2;      \
    pt2             = temp

/* Exported functions --------------------------------------------------------*/
esp_err_t spotify_client_init(UBaseType_t priority, QueueHandle_t *playing_q_hlr) {
    init_functions_cb();

    esp_http_client_config_t config = {
        .url           = "https://api.spotify.com/v1",
        .event_handler = _http_event_handler,
        .cert_pem      = spotify_cert_pem_start,
    };
    state.client    = esp_http_client_init(&config);
    state.buffer_cb = default_fun;

    client_lock = xSemaphoreCreateMutex();

    int res = xTaskCreate(currently_playing_task, "currently_playing_task", 4096, NULL, priority, &playing_task_hlr);
    if (res == pdPASS) {
        /* Create a queue for events from the playing task. Tasks can
           read from this queue to receive up to date playing track.*/
        playing_queue_hlr = xQueueCreate(1, sizeof(TrackInfo));
        *playing_q_hlr    = playing_queue_hlr;
        return ESP_OK;
    }
    return ESP_FAIL;
}

void player_cmd(rotary_encoder_event_t *event) {
    Player_cmd_t cmd;

    if (event->event_type == BUTTON_EVENT) {
        cmd = cmdToggle;
    } else {
        cmd = event->re_state.direction == ROTARY_ENCODER_DIRECTION_CLOCKWISE ? cmdNext : cmdPrev;
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
            ESP_LOGE(TAG, "unknow command");
            return;
    }

    ACQUIRE_LOCK(client_lock);
    state.buffer_cb = default_fun;
    _validate_token();
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
            vTaskDelay(pdMS_TO_TICKS(1000)); /* wait for the server to update the current track */
            xTaskNotifyGive(playing_task_hlr);
        }
    } else {
        _handle_err_connection();
        goto retry;
    }

    RELEASE_LOCK(client_lock);

    ESP_LOGW(TAG, "[PLAYER-TASK]: uxTaskGetStackHighWaterMark(): %d",
             uxTaskGetStackHighWaterMark(NULL));
}

void http_user_playlists() {
    ACQUIRE_LOCK(client_lock);
    state.buffer_cb = get_playlists;
    state.method    = HTTP_METHOD_GET;
    // state.endpoint = PLAYERENDPOINT("/me/playlists?offset=0&limit=4");
    state.endpoint = PLAYERENDPOINT("/me/playlists?offset=0&limit=50");
    _validate_token();
    PREPARE_CLIENT(state, state.access_token, "application/json");
    ESP_LOGW(TAG, "Endpoint to send: %s", state.endpoint);
    state.err         = esp_http_client_perform(state.client);
    state.status_code = esp_http_client_get_status_code(state.client);

    RELEASE_LOCK(client_lock);
    /* if (state.err == ESP_OK) {
        ESP_LOGI(TAG, "Received:\n%s", buffer);
    } */
}

/* Private functions ---------------------------------------------------------*/

static esp_err_t _validate_token() {
    /* client_lock lock already must be aquired */

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

static void _get_active_devices(StrList *devices) {
    /* client_lock lock already must be aquired */

    state.endpoint = PLAYERENDPOINT(PLAYER "/devices");
    state.method   = HTTP_METHOD_GET;
    _validate_token();
    PREPARE_CLIENT(state, state.access_token, "application/json");

    state.err         = esp_http_client_perform(state.client);
    state.status_code = esp_http_client_get_status_code(state.client);
    esp_http_client_set_post_field(state.client, NULL, 0);

    ESP_LOGW(TAG, "Active devices:\n%s", buffer);

    //*devices = malloc(sizeof(*devices));
    available_devices(buffer, devices);
}

static void _track_info_free(TrackInfo *track) {
    free(track->name);
    strListClear(&(track->artists));
    free(track->album);
    free(track->device.id);
    free(track->device.name);
}

static void _handle_200_response(TrackInfo **new_track) {
    int val = parseTrackInfo(buffer, *new_track);

    if (eTrackAllParsed != val) {
        ESP_LOGE(TAG, "Error parsing track. Flags parsed: %x", val);
        ESP_LOGE(TAG, "\n%s", buffer);
    } else {
        if (strcmp(curr_trk_ptr->name, (*new_track)->name) != 0) {
            _track_info_free(curr_trk_ptr);

            SWAP_POINTERS(*new_track, curr_trk_ptr);
            ESP_LOGI(TAG, "***** New Track *****");
            ESP_LOGI(TAG, "Title: %s", curr_trk_ptr->name);
            StrListItem *artist = curr_trk_ptr->artists.first;
            while (artist) {
                ESP_LOGI(TAG, "Artist: %s", artist->str);
                artist = artist->next;
            }
            ESP_LOGI(TAG, "Album: %s", curr_trk_ptr->album);

            xQueueOverwrite(playing_queue_hlr, curr_trk_ptr);
        } else {
            _track_info_free(*new_track);
        }
    }
}

static void _handle_err_connection() {
    ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(state.err));
    if (retries > 0 && ++retries <= RETRIES_ERR_CONN) {
        ESP_LOGW(TAG, "Retrying %d/%d...", retries, RETRIES_ERR_CONN);
    } else {
        ESP_LOGW(TAG, "Restarting...");
        esp_restart();
    }
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    state.buffer_cb(buffer, evt);
    return ESP_OK;
}

static void currently_playing_task(void *pvParameters) {
    bool on_try_current_dev = false;

    TrackInfo  track       = {.name = ""};
    TrackInfo *new_trk_ptr = &track;

    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));  // TODO: config FETCH_FREQ_MS

        ACQUIRE_LOCK(client_lock);
        state.buffer_cb = default_fun;
        state.method    = HTTP_METHOD_GET;
        state.endpoint  = PLAYERENDPOINT(PLAYING);

    prepare:
        _validate_token();
        PREPARE_CLIENT(state, state.access_token, "application/json");

    retry:;
        state.err         = esp_http_client_perform(state.client);
        state.status_code = esp_http_client_get_status_code(state.client);
        esp_http_client_set_post_field(state.client, NULL, 0); /* Clear post field */
        if (state.err == ESP_OK) {
            ESP_LOGD(TAG, "Received:\n%s", buffer);
            if (state.status_code == 200) {
                retries = 0;
                _handle_200_response(&new_trk_ptr);
                goto exit;
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
                    _get_active_devices(&devices);

                    StrListItem *dev = devices.first;  // for now we pick the first device on the list
                    if (dev) {
                        snprintf(buf, 100, "{\"device_ids\":[\"%s\"],\"play\":%s}", dev->str, "true");
                    } else {
                        ESP_LOGE(TAG, "No devices found :c");
                        goto exit;
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
                // ESP_LOGI(TAG, "Playback transfered to: %s", curr_trk_ptr->device.id); //posible crash here
                on_try_current_dev = false;
                goto exit;
            }
            /* Unhandled status_code follows */
            ESP_LOGE(TAG, "ENDPOINT: %s, METHOD: %s, STATUS_CODE: %d",
                     state.endpoint, HTTP_METHOD_LOOKUP[state.method], state.status_code);
            if (*buffer) {
                ESP_LOGE(TAG, buffer);
            }
            goto exit;

        } else {
            _handle_err_connection();
            goto retry;
        }
    exit:
        RELEASE_LOCK(client_lock);
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