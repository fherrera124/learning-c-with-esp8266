#ifndef INCLUDE_PARSEOBJECTS_H_
#define INCLUDE_PARSEOBJECTS_H_

#include <stdbool.h>
#include <time.h>

#include "strlib.h"

typedef enum {
    eTrackNameParsed      = 0x01,
    eTrackArtistsParsed   = 0x02,
    eTrackAlbumParsed     = 0x04,
//  eTrackDurationParsed  = 0x08,
//  eTrackProgressParsed  = 0x10,
    eTrackIsPlayingParsed = 0x20,
    eTrackDeviceParsed    = 0x40,
    eTrackAllParsed       = eTrackNameParsed |
                      eTrackArtistsParsed |
                      eTrackAlbumParsed |
//                    eTrackDurationParsed |
//                    eTrackProgressParsed |
                      eTrackIsPlayingParsed |
                      eTrackDeviceParsed
} TrackParsed;

typedef enum {
    eTokensAccessParsed    = 0x01,
//  eTokensRefreshParsed   = 0x02,
    eTokensExpiresInParsed = 0x04,
    eTokensAllParsed       = eTokensAccessParsed |
//                     eTokensRefreshParsed |
                       eTokensExpiresInParsed
} TokensParsed;

typedef struct
{
    char *id;
    bool  is_active;
    char *name;
    char *type;
    char *volume_percent;
} Device;

typedef struct
{
    char       *name;
    StrList     artists;
    char       *album;
    int         duration;
    int         progress;
    bool        isPlaying;
    Device      device;
    TrackParsed parsed;
} TrackInfo;

typedef struct
{
/*  char        *refreshToken;
    char        *authToken; */
    char        *access_token;
    time_t       expiresIn;
    TokensParsed parsed;
} Tokens;

void init_functions_cb(void);

TrackParsed  parseTrackInfo(const char *js, TrackInfo *track);
TokensParsed parseTokens(const char *js, Tokens *tokens);
void available_devices(const char *js, StrList * device_list);

#endif /* INCLUDE_PARSEOBJECTS_H_ */