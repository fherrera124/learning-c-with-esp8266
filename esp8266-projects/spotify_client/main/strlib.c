#include "strlib.h"

#include <stdio.h>
#include <stdlib.h>

StrListItem *allocStrListItem(char *str) {
    StrListItem *item = (StrListItem *)malloc(sizeof(StrListItem));
    if (item) {
        item->str  = str;
        item->next = NULL;
    }
    return item;
}

void strListAppend(StrList *list, char *str) {
    StrListItem *item = allocStrListItem(str);
    if (!item) {
        return;
    }
    if (!list->first) {
        list->first = item;
        list->last  = item;
        list->count = 1;
    } else {
        list->last->next = item;
        list->last       = item;
        list->count++;
    }
}