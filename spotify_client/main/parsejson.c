#include "parsejson.h"

#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "jsmn.h"

#define MAX_TOKENS 600

static const char *TAG = "PARSE_JSON";

void parsejson(const char *json, KeyCbPair *callbacks, size_t callbacksSize, void *object) {
    jsmntok_t *tokens = (jsmntok_t *)malloc(sizeof(jsmntok_t) * MAX_TOKENS);

    jsmn_parser jsmn;
    jsmn_init(&jsmn);

    jsmnerr_t n = jsmn_parse(&jsmn, json, strlen(json), tokens, MAX_TOKENS);

    if (n < 0) {
        ESP_LOGE(TAG, "Parse error: %s\n", error_str(n));
    } else {
        jsmntok_t *root = &tokens[0];
        for (size_t i = 0; i < callbacksSize; i++) {
            KeyCbPair *keyCb = &callbacks[i];
            keyCb->callback(json, root, object);
        }
    }
    free(tokens);
}
