#include "parseobjects.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "jsmn.h"

typedef void (*PathCb)(const char *, void *, void *);

#define TRACK_CALLBACKS_SIZE 4
PathCb trackCallbacks[TRACK_CALLBACKS_SIZE];

#define TOKENS_CALLBACKS_SIZE 2
PathCb tokensCallbacks[TOKENS_CALLBACKS_SIZE];

#define MAX_TOKENS 600

static const char *TAG = "PARSE_OBJECT";

void onTrackName(const char *json, void *root, void *object) {
    TrackInfo *track = (TrackInfo *)object;

    jsmntok_t *value = object_get_member(json, root, "item");
    if (!value) return;
    value = object_get_member(json, value, "name");
    if (!value) return;

    const char *track_name = json + value->start;

    track->name = strndup(track_name, value->end - value->start);

    track->parsed |= eTrackNameParsed;

    ESP_LOGI(TAG, "Track name: %s", track->name);
}

void onArtistsName(const char *json, void *root, void *object) {
    TrackInfo *track = (TrackInfo *)object;

    jsmntok_t *value = object_get_member(json, root, "item");
    if (!value) return;
    value = object_get_member(json, value, "artists");
    if (!value) return;
    const char *artists = json + value->start;
    // TODO: transform list of artists
    track->parsed |= eTrackArtistsParsed;
    ESP_LOGI(TAG, "Artists: %.*s", value->end - value->start, artists);
}

void onAlbumName(const char *json, void *root, void *object) {
    TrackInfo *track = (TrackInfo *)object;

    jsmntok_t *value = object_get_member(json, root, "item");
    if (!value) return;
    value = object_get_member(json, value, "album");
    if (!value) return;
    value = object_get_member(json, value, "name");
    if (!value) return;
    const char *album = json + value->start;
    // TODO: transform list of ...
    track->parsed |= eTrackAlbumParsed;
    ESP_LOGI(TAG, "Album name: %.*s", value->end - value->start, album);
}

void onTrackIsPlaying(const char *json, void *root, void *object) {
    TrackInfo *track = (TrackInfo *)object;

    jsmntok_t *value = object_get_member(json, root, "is_playing");
    if (!value) return;
    char type        = (json + (value->start))[0];
    track->isPlaying = type == 't' ? true : false;
    track->parsed |= eTrackIsPlayingParsed;
}

void onAccessToken(const char *json, void *root, void *object) {
    Tokens *token = (Tokens *)object;

    jsmntok_t *value = object_get_member(json, root, "access_token");
    if (!value) return;
    token->accessToken = strndup(json + value->start, value->end - value->start);
    token->parsed |= eTokensAccessParsed;
}

static int str2int(const char *str, short len) {
    int ret = 0;
    for (short i = 0; i < len; ++i) {
        ret = ret * 10 + (str[i] - '0');
    }
    return ret;
}

void onExpiresIn(const char *json, void *root, void *object) {
    Tokens    *token = (Tokens *)object;
    jsmntok_t *value = object_get_member(json, root, "expires_in");
    if (!value) return;
    int seconds      = str2int(json + value->start, value->end - value->start);
    token->expiresIn = time(0) + seconds;
    token->parsed |= eTokensExpiresInParsed;
}

void parsejson(const char *json, PathCb *callbacks, size_t callbacksSize, void *object) {
    jsmntok_t *tokens = (jsmntok_t *)malloc(sizeof(jsmntok_t) * MAX_TOKENS);

    jsmn_parser jsmn;
    jsmn_init(&jsmn);

    jsmnerr_t n = jsmn_parse(&jsmn, json, strlen(json), tokens, MAX_TOKENS);

    if (n < 0) {
        ESP_LOGE(TAG, "Parse error: %s\n", error_str(n));
    } else {
        jsmntok_t *root = &tokens[0];
        for (size_t i = 0; i < callbacksSize; i++) {
            PathCb *keyCb = &callbacks[i];
            (*keyCb)(json, root, object);
        }
    }
    free(tokens);
}

void init_functions_cb() {
    trackCallbacks[0] = onTrackName;
    trackCallbacks[1] = onArtistsName;
    trackCallbacks[2] = onAlbumName;
    trackCallbacks[3] = onTrackIsPlaying;

    tokensCallbacks[0] = onAccessToken;
    tokensCallbacks[1] = onExpiresIn;
}

TrackParsed parseTrackInfo(const char *json, TrackInfo *track) {
    parsejson(json, trackCallbacks, TRACK_CALLBACKS_SIZE, track);
    return track->parsed;
}

TokensParsed parseTokens(const char *json, Tokens *tokens) {
    parsejson(json, tokensCallbacks, TOKENS_CALLBACKS_SIZE, tokens);
    return tokens->parsed;
}