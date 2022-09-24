#include "parseobjects.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "jsmn.h"
#include "parsejson.h"

static const char *TAG = "PARSE_TRACK";

void onTrackName(const char *, void *, void *);
void onArtistsName(const char *, void *, void *);
void onAlbumName(const char *, void *, void *);
void onAccessToken(const char *, void *, void *);
void onExpiresIn(const char *, void *, void *);

#define TRACK_CALLBACKS_SIZE 3  // orig: 7
KeyCbPair trackCallbacks[TRACK_CALLBACKS_SIZE];

#define TOKENS_CALLBACKS_SIZE 2
KeyCbPair tokensCallbacks[TOKENS_CALLBACKS_SIZE];

void initPaths() {
    bindKeyCb(trackCallbacks[0], "name", onTrackName);
    bindKeyCb(trackCallbacks[1], "artists", onArtistsName);
    bindKeyCb(trackCallbacks[2], "album", onAlbumName);

    bindKeyCb(tokensCallbacks[0], "access_token", onAccessToken);
    // bindKeyhCb(tokensCallbacks[1], "refresh_token", onRefreshToken);
    bindKeyCb(tokensCallbacks[1], "expires_in", onExpiresIn);
}

TrackParsed parseTrackInfo(const char *json, TrackInfo *track) {
    parsejson(json, trackCallbacks, TRACK_CALLBACKS_SIZE, track);
    return track->parsed;
}

TokensParsed parseTokens(const char *json, Tokens *tokens) {
    parsejson(json, tokensCallbacks, TOKENS_CALLBACKS_SIZE, tokens);
    return tokens->parsed;
}

void onTrackName(const char *json, void *root, void *object) {
    TrackInfo *track = (TrackInfo *)object;

    jsmntok_t *value = object_get_member(json, root, "item");
    value            = object_get_member(json, value, "name");

    if (value != NULL) {
        char *track_name = json + value->start;

        track->name = strndup(track_name, value->end - value->start);

        track->parsed |= eTrackNameParsed;

        ESP_LOGI(TAG, "Track name: %s", track->name);
    }
}

void onArtistsName(const char *json, void *root, void *object) {
    TrackInfo *track = (TrackInfo *)object;

    jsmntok_t *value = object_get_member(json, root, "item");
    value            = object_get_member(json, value, "artists");

    if (value != NULL) {
        char *artists = json + value->start;
        // TODO: transform list of artists
        track->parsed |= eTrackArtistsParsed;
        ESP_LOGI(TAG, "Artists: %.*s", value->end - value->start, artists);
    }
}

void onAlbumName(const char *json, void *root, void *object) {
    TrackInfo *track = (TrackInfo *)object;

    jsmntok_t *value = object_get_member(json, root, "item");
    value            = object_get_member(json, value, "album");
    value            = object_get_member(json, value, "name");

    if (value != NULL) {
        char *album = json + value->start;
        // TODO: transform list of ...
        track->parsed |= eTrackAlbumParsed;
        ESP_LOGI(TAG, "Album name: %.*s", value->end - value->start, album);
    }
}

void onAccessToken(const char *json, void *root, void *object) {
    Tokens *token = (Tokens *)object;

    jsmntok_t *value = object_get_member(json, root, "access_token");
    if (value != NULL) {
        token->accessToken = strndup(json + value->start, value->end - value->start);
        token->parsed |= eTokensAccessParsed;
    }
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
    if (value != NULL) {
        int seconds      = str2int(json + value->start, value->end - value->start);
        token->expiresIn = time(0) + seconds;
        token->parsed |= eTokensExpiresInParsed;
    }
}