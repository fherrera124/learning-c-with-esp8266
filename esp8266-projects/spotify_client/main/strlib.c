#include "strlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void strListClear(StrList *list) {
    StrListItem *item = list->first;
    StrListItem *aux;

    while (item) {
        free(item->str);
        aux = item->next;
        free(item);
        item = aux;
    }
    list->first = NULL;
    list->last  = NULL;
    list->count = 0;
}

int strListEqual(StrList *list1, StrList *list2) {
    if (list1->count != list2->count) {
        return FALSE;
    }
    StrListItem *item1 = list1->first;
    StrListItem *item2 = list2->first;
    while (item1 && item2) {
        if (strcmp(item1->str, item2->str)) {
            return FALSE;
        }
        item1 = item1->next;
        item2 = item2->next;
    }
    return TRUE;
}