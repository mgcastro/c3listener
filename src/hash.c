/* Hash table -
 *
 *   Shawn Nock 2015
 * 
 */

#include <stdlib.h>

#include "c3listener.h"
#include "hash.h"

static void* hashtable[HASH_TABLE_LENGTH] = { NULL };

static int hash_index(void *obj, index_cb index) {
  return index(obj) % HASH_TABLE_LENGTH;
}

void hash_delete(void *obj, index_cb index, equal_p equal) {
  hashable_t *v = hash_find(obj, index, equal);
  if (v != NULL) {
    if (v->prev == NULL) {
      /* Beacon is root of linked list */
      int idx = hash_index(obj, index);
      if (v->next != NULL) {
	hashtable[idx] = v->next;
      } else {
	v = hashtable[idx];
	hashtable[idx] = NULL;
      }
    } else {
      if (v->next != NULL) {
	/* Beacon is middle of list */
        v->prev->next = v->next;
	v->next->prev = v->prev;
      } else {
	/* Beacon is end of LL */
	v->prev->next = NULL;
      }
    }
    /* Make sure v points to the removed node */
    free(v);
  }
}

void *hash_find(void *obj, index_cb index, equal_p equal) {
  hashable_t *v = hashtable[index(obj) % HASH_TABLE_LENGTH];
  do {
    if (obj == v || equal(obj, v)) {
      return v;
    }
  } while ((v = v->next));
  return NULL;
}

void *hash_add(void *obj, index_cb index, equal_p equal) {
  /* Expects obj to be a malloc'd region or otherwise persistant until
     hash_delete is called */
  int idx = hash_index(obj, index);
  hashable_t *v = NULL, *v_last = NULL;
  
  if ((v = hashtable[idx]) != NULL) {
    /* obj or collision exists */
    do {
      v_last = v;
      if (obj == v || equal(obj, v)) {
	/* A match */
	/* log_stdout("Hit\n"); */
	return v;
      } else {
	log_stdout("Collision\n");
	/* Collision */
      }
    } while ((v = v->next));
  }
  /* The object isn't in the table, insert it. Hang the beacon off
     last collision, or on the hash table directly */
  v = obj;
  if (v_last != NULL) {
    v_last->next = v;
    v->prev = v_last;
    v->next = NULL;
  } else {
    hashtable[idx] = v;
    v->prev = v->next = NULL;
  }
  /* log_stdout("Created\n"); */
  return v;
}

void hash_walk(walker_cb *walker, void **args, size_t size) {
  if (size < 1) {
    /* We don't have any work todo, skip walk */
    return;
  }
  for (int i = 0; i < HASH_TABLE_LENGTH; i++) {
    hashable_t *v = hashtable[i];
    while (v != NULL) {
      /* log_stdout("Running walker on %p\n", v); */
      for (int j = 0; j < size; j++) {
	v = walker[j](v, args[j]);
	if (v == NULL) {
	  break;
	}
      }
      if (v == NULL) {
	/* walker functions can NULL v, i.e. if the object is
	   deleted. We cannot reason about any other chained nodes. */
	continue;
      }
      v = v->next;
    }
    /* log_stdout("Checking idx: %d\n", i); */
  }
}  
