#ifndef INCLUDE_PARSEOBJECTS_H_
#define INCLUDE_PARSEOBJECTS_H_

#include "strlib.h"

typedef enum {
    eTrackNameParsed      = 0x01,
    eTrackArtistsParsed   = 0x02,
    eTrackAlbumParsed     = 0x04,
    eTrackDurationParsed  = 0x08,
    eTrackProgressParsed  = 0x10,
    eTrackIsPlayingParsed = 0x20,
    eTrackAllParsed       = eTrackNameParsed |
                      eTrackArtistsParsed |
                      eTrackAlbumParsed |
                      eTrackDurationParsed |
                      eTrackProgressParsed |
                      eTrackIsPlayingParsed
} TrackParsed;

typedef struct
{
    char       *name;
    StrList     artists;
    StrList     album;
    int         duration;
    int         progress;
    int         isPlaying;
    TrackParsed parsed;
} TrackInfo;

typedef struct
{
    char *accessToken;
    int   accessTokenSize;
    char *refreshToken;
    int   refreshTokenSize;
    int   expiresIn;
    // TokensParsed parsed;
} Tokens;

void initPaths(void);

TrackParsed parseTrackInfo(const char *json, TrackInfo *track);

#endif /* INCLUDE_PARSEOBJECTS_H_ */