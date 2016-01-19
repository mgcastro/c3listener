#ifndef __HASH_H
#define __HASH_H

#include <stdbool.h>
#include <stddef.h>

#define HASH_TABLE_LENGTH 251 /* Ought to be prime and approximately
				 as large as expected number of
				 entries for best performance */

typedef struct generic_hashtable_obj {
  struct generic_hashtable_obj *next, *prev;
} hashable_t;

typedef bool (*equal_p)(void *a, void *b);
typedef int (*index_cb)(void *);
typedef void *(*walker_cb)(void *, void *);
typedef bool (*cond_p)(void *);

void hash_delete(void *, index_cb, equal_p);
void *hash_find(void *, index_cb, equal_p);
void *hash_add(void *, index_cb, equal_p);
void hash_walk(walker_cb*, void**, size_t);

#endif /* __HASH_H */
