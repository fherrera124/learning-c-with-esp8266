/* Includes ------------------------------------------------------------------*/
#include "buffer_callbacks.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_tls.h"
#include "freertos/task.h"

/* Private macro -------------------------------------------------------------*/
#define MAX_HTTP_BUFFER 8192

#define MAX_TOKENS 100 /* We expect no more than 100 JSON tokens */

#define MATCH_NEXT_CHAR(data, ch, left)                    \
    if (END == skip_blanks(&data, &left) || *data != ch) { \
        state.abort = true;                                \
        xTaskNotifyGive(menu_task_hlr);                    \
        return;                                            \
    }                                                      \
    data++;                                                \
    left--;

#define MATCH_KEY(data, str, left)      \
    strncpy(buffer, data, left);        \
    buffer[left] = '\0';                \
    char *tmp    = data;                \
    data         = strstr(data, str);   \
    if (data == NULL) {                 \
        printf("%s not found\n", str);  \
        state.abort = true;             \
        xTaskNotifyGive(menu_task_hlr); \
        return;                         \
    } else {                            \
        data += strlen(str);            \
        left -= (data - tmp);           \
    }

/* Private types -------------------------------------------------------------*/
typedef union {
    struct {
        uint32_t first_chunk : 1; /*!< First chunk of data */
        uint32_t abort       : 1; /*!< Found an error, skip all chunks */
        uint32_t get_new_obj : 1; /*!< Get New object */
        uint32_t finished    : 1; /*!< We're done */
    };
    uint32_t val; /*!< union fill */
} StateRequest_t;

typedef enum {
    END,
    MATCH,
    UNMATCH,
    BLANK,
    CHAR,
} match_result_t;

/* Private function prototypes -----------------------------------------------*/
esp_err_t str_append(char **str, jsmntok_t *obj, const char *buffer);
match_result_t static skip_blanks(char **ptr, int *left);
esp_err_t process_JSON_obj(char *buffer, int output_len);

/* Private variables ---------------------------------------------------------*/
static int         output_len;  // Stores number of bytes read
static const char *TAG = "HTTP-BUFFER";
static int         curly_count;

/* External variables --------------------------------------------------------*/
extern TaskHandle_t menu_task_hlr;
extern Playlists_t  playlists;

/* Exported functions --------------------------------------------------------*/
void default_fun(char *buffer, esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if ((output_len + evt->data_len) > MAX_HTTP_BUFFER) {
                ESP_LOGE(TAG, "Not enough space on buffer. Ignoring incoming data.");
                return;
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
                buffer[output_len] = 0;
                output_len         = 0;
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Process each playlist JSON object of array "items" : [{pl1},{pl2}...{plz}]
 * since there is no enough memory to fetch the whole JSON.
 *
 * @param buffer
 * @param evt
 */
void get_playlists(char *buffer, esp_http_client_event_t *evt) {
    static StateRequest_t state = {{true, false, true, false}};

    char *data = (char *)evt->data;
    int   left = evt->data_len;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:

            if (state.abort || state.finished) return;

            if (state.first_chunk) {
                state.first_chunk = false;

                MATCH_KEY(data, "\"items\"", left);
                MATCH_NEXT_CHAR(data, ':', left);
                MATCH_NEXT_CHAR(data, '[', left);
            }
        get_new_obj:
            if (state.get_new_obj) {
                if (END == skip_blanks(&data, &left)) {
                    return;
                }
                if (*data == '{') {
                    output_len           = 0;
                    state.get_new_obj    = false;
                    curly_count          = 1;
                    buffer[output_len++] = *data;
                    data++;
                    left--;
                } else {
                    ESP_LOGE(TAG, "'{' not found. Instead: '%c'\n", *data);
                    state.abort = true;
                    xTaskNotifyGive(menu_task_hlr);
                    return;
                }
            }

            do {
                buffer[output_len] = *data;
                output_len++;
                if (*data == '{') {
                    curly_count++;
                } else if (*data == '}') {
                    curly_count--;
                }
                data++;
                left--;
            } while (left > 0 && curly_count != 0);

            if (curly_count == 0) {
                if (ESP_OK != process_JSON_obj(buffer, output_len)) {
                    state.abort = true;
                    xTaskNotifyGive(menu_task_hlr);
                    return;
                }
                if (left > 0 && END != skip_blanks(&data, &left)) {
                    if (*data == ',') {
                        data++;
                        left--;
                        state.get_new_obj = true;
                        goto get_new_obj;  // new object, and still data in current chunk
                    } else if (*data == ']') {
                        state.finished = true;
                        xTaskNotifyGive(menu_task_hlr);
                    } else {
                        ESP_LOGE(TAG, "Unexpected character '%c'. Abort\n", *data);
                        state.abort = true;
                        xTaskNotifyGive(menu_task_hlr);
                        return;
                    }
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            output_len = 0;
            state.val  = 5;  // 0101
            break;
        case HTTP_EVENT_DISCONNECTED:;
            if (ESP_OK != esp_tls_get_and_clear_last_error(evt->data, NULL, NULL)) {
                output_len = 0;
                state.val  = 5;  // 0101
            }
            break;
        default:
            break;
    }
}

/* Private functions ---------------------------------------------------------*/
match_result_t static skip_blanks(char **ptr, int *left) {
    while (*left > 0 && isspace(**ptr)) {
        output_len++;
        *left = *left - 1;
        *ptr  = *ptr + 1;
    }
    if (!isspace(**ptr)) { //TODO: revisar
        return CHAR;
    } else {
        printf("END mm\n");
        return END;
    }
}

esp_err_t process_JSON_obj(char *buffer, int output_len) {
    jsmn_parser jsmn;
    jsmn_init(&jsmn);

    jsmntok_t *tokens = malloc(sizeof(jsmntok_t) * MAX_TOKENS);
    if (!tokens) {
        ESP_LOGE(TAG, "tokens not allocated");
        return ESP_FAIL;
    }

    jsmnerr_t n = jsmn_parse(&jsmn, buffer, output_len, tokens, MAX_TOKENS);
    output_len  = 0;
    if (n < 0) {
        ESP_LOGE(TAG, "Parse error: %s\n", error_str(n));
        goto fail;
    }

    jsmntok_t *name = object_get_member(buffer, tokens, "name");
    if (!name) {
        ESP_LOGE(TAG, "NAME missing");
        goto fail;
    }

    jsmntok_t *uri = object_get_member(buffer, tokens, "uri");
    if (!uri) {
        ESP_LOGE(TAG, "URI missing");
        goto fail;
    }

    esp_err_t err = str_append(&playlists.name_list, name, buffer);

    free(tokens);
    return err;
fail:
    free(tokens);
    return ESP_FAIL;
}

esp_err_t str_append(char **str, jsmntok_t *obj, const char *buffer) {
    if (*str == NULL) {
        *str = jsmn_obj_dup(buffer, obj);
        return (*str == NULL) ? ESP_FAIL : ESP_OK;
    }

    int obj_len = obj->end - obj->start;
    int str_len = strlen(*str);

    char *r = realloc(*str, str_len + obj_len + 2);
    if (r == NULL) {
        return ESP_FAIL;
    }
    *str = r;

    (*str)[str_len++] = '\n';

    for (int i = 0; i < obj_len; i++) {
        (*str)[i + str_len] = *(buffer + obj->start + i);
    }
    (*str)[str_len + obj_len] = '\0';

    return ESP_OK;
}
