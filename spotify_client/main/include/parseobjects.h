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
    eTrackAllParsed       = eTrackNameParsed |
                      eTrackArtistsParsed |
                      eTrackAlbumParsed |
//                    eTrackDurationParsed |
//                    eTrackProgressParsed |
                      eTrackIsPlayingParsed
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
    char       *name;
    StrList     artists;
    StrList     album;
    int         duration;
    int         progress;
    bool        isPlaying;
    TrackParsed parsed;
} TrackInfo;

typedef struct
{
    /*  char        *refreshToken;
        char        *authToken; */
    char        *accessToken;
    time_t       expiresIn;
    TokensParsed parsed;
} Tokens;

void init_functions_cb(void);

TrackParsed  parseTrackInfo(const char *json, TrackInfo *track);
TokensParsed parseTokens(const char *json, Tokens *tokens);

#endif /* INCLUDE_PARSEOBJECTS_H_ */