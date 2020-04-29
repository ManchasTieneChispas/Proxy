/* @author: William Giraldo (wgiraldo)
 *
 * This file consists of prototypes and definitions for cache.c
 *
 * These files implement a simple cache. Inteded for use with proxy.c, but
 * can be utilized elsewhere.
 * The cache is implemented via a doubly linked list, with key value pairs.
 * Each object in the cache has max size MAX_OBJECT_SIZE
 * The maximum cache size is MAX_CACHE_SIZE
 *
 */

#include <stdlib.h>

// max cache and cache object size
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/* Type for each cache object
 *
 * next is the next object in the cache
 * prev is the previous object in the cache
 * key is the key to confirm if this is desired element
 * buf is the data held by the cache
 * ref is how many thread current hold a reference to buf
 * size is the size of the object
 */
typedef struct object {
    struct object *next;
    struct object *prev;
    char *key;
    char *buf;
    int ref;
    size_t size;
} obj_t;

/* Type for the cache
 *
 * This implements a doubly linked list
 *
 * start is the beginning of the list
 * end is the end of the list
 * size is the current size of the cache data, not counting keys and structures
 */
typedef struct {
    obj_t *start;
    obj_t *end;
    size_t size;
} cache_t;
