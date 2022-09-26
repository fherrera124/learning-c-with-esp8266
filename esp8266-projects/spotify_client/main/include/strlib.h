#ifndef STRLIB_H
#define STRLIB_H

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

void strListAppend(StrList *list, char *str);
void strListClear(StrList *list);

#endif /* STRLIB_H */