#ifndef STRLIB_H
#define STRLIB_H

#include "esp_err.h"
#include "typedefs.h"

/* typedef struct
{
    ushort *str;
    int     length;
} StrBuf; */

typedef struct StrListItem StrListItem;
typedef struct StrList     StrList;

struct StrListItem {
    char        *str;
    StrListItem *next;
};
struct StrList {
    StrListItem *first;
    StrListItem *last;
    int          count;
};

esp_err_t strListAppend(StrList *list, char *str);
void      strListClear(StrList *list);
int       strListEqual(StrList *list1, StrList *list2);

#endif /* STRLIB_H */