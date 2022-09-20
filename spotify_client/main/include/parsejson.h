#ifndef INCLUDE_PARSEJSON_H_
#define INCLUDE_PARSEJSON_H_

#include <stdarg.h>
#include <stddef.h>

typedef void (*PathCb)(const char *, void *, void *);

typedef struct
{
    char  *key;
    PathCb callback;
} KeyCbPair;

#define bindKeyCb(pair, k, cb) \
    pair.key      = k;         \
    pair.callback = cb

void parsejson(const char *json, KeyCbPair *callbacks, size_t callbacksSize, void *object);

#endif /* INCLUDE_PARSEJSON_H_ */
