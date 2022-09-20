#include "parseobjects.h"

#include <string.h>

#include "esp_log.h"
#include "jsmn.h"
#include "parsejson.h"

static const char *TAG = "PARSE_TRACK";

void onTrackName(const char *value, void *root, void *object);
void onArtistsName(const char *value, void *root, void *object);
void onAlbumName(const char *value, void *root, void *object);

#define TRACK_CALLBACKS_SIZE 3  // orig: 7
KeyCbPair trackCallbacks[TRACK_CALLBACKS_SIZE];

void initPaths() {
    bindKeyCb(trackCallbacks[0], "name", onTrackName);
    bindKeyCb(trackCallbacks[1], "artists", onArtistsName);
    bindKeyCb(trackCallbacks[2], "album", onAlbumName);
}

TrackParsed parseTrackInfo(const char *json, TrackInfo *track) {
    parsejson(json, trackCallbacks, TRACK_CALLBACKS_SIZE, track);

    return track->parsed;
}

void onTrackName(const char *json, void *root, void *object) {
    TrackInfo *track = object;

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
    TrackInfo *track = object;

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
    TrackInfo *track = object;

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