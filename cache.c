/* @author William Giraldo (wgiraldo)
 *
 * This file implements a cache
 * It is intended for use with proxy.c
 *
 * To implement the cache a doubly linked cache is used. See cache.h for more
 */

#include "cache.h"
#include "csapp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// global cache variable
cache_t *cache = NULL;

/* returns the size of current cached data */
size_t get_cache_size() {
    return cache->size;
}

/* returns the maximum cache size */
size_t get_max_cache_size() {
    return MAX_CACHE_SIZE;
}

/* decreases the ref count of obj. MUST be called if a user finishes with an obj
 *
 * Should only be called if a user will not use obj again until another call
 * to get_obj
 */
void done_with(obj_t *obj) {
    obj->ref -= 1;
}

/* Checks the cache for the LRU object, and removes it to make space for another
 *
 * This will not evict a object if the reference count is more than 0, and
 * will hang until it can remove it
 */
void evict() {
    // because of our implemtation, we know that the LRU object is the last one
    obj_t *to_leave = cache->end;

    // if we have a ref to the object, we cant evict yet, so recurse
    if (to_leave->ref > 0) {
        evict();
        return;
    }

    // remove the object from the cache
    if (cache->start == cache->end) {
        cache->start = NULL;
        cache->end = NULL;
    } else {
        cache->end = to_leave->prev;
    }

    // update cache size
    cache->size -= to_leave->size;

    // free memory
    free(to_leave->key);
    free(to_leave->buf);
    free(to_leave);
}

/* moves a object in the cache to the front of the linked list
 *
 * obj must be in the cache already
 */
void move_to_front(obj_t *obj) {
    // if there is only one element we are already at front, or at front already
    if (cache->start == cache->end || cache->start == obj) {
        return;
    } else if (cache->end == obj) { // if we are at the end
        cache->end = obj->prev;
        cache->end->next = NULL;
        obj->prev = NULL;
        obj->next = cache->start;
        cache->start->prev = obj;
        cache->start = obj;
    } else { // if obj is not the start or the end
        obj->prev->next = obj->next;
        obj->next->prev = obj->prev;
        obj->prev = NULL;
        obj->next = cache->start;
        cache->start->prev = obj;
        cache->start = obj;
    }
}

/* Finds an object in the cache with a matching key. Returns NULL if none
 *
 * This returns an obj_t, not the buf, so that the user can decrease ref count
 * when they are done using it
 *
 * requires that cache_init has been called previously
 */
obj_t *get_obj(char *key) {
    // traverse the cache to find a matchign key and obj_t
    for (obj_t *curr = cache->start; curr != cache->end; curr = curr->next) {
        // if the keys are equal then we found the obj
        if (strcmp(curr->key, key) == 0) {
            // move to the front of the list
            move_to_front(curr);

            // increase ref count
            curr->ref += 1;

            return curr;
        }
    }

    // if nothing was found, return NULL
    return NULL;
}

/* Adds a object to the cache
 *
 * Stores the pointer to the object, not the ojects data itself
 * If adding this object would cause the cache to exceed MAX_CACHE_SIZE, then
 * it will call evict() to make room
 *
 * The size of the object must be less thatn MAX_OBJECT_SIZE
 * cache_init must be called before any call to add_obj
 */
void add_obj(char *key, char *buf, size_t buf_size) {
    // check if adding object would exceed MAX_CACHE_SIZE, if so make room
    for (size_t curr_size = cache->size; curr_size + buf_size > MAX_CACHE_SIZE;
         curr_size = cache->size) {
        evict();
    }

    // allocate space for new object
    obj_t *new = Malloc(sizeof(obj_t));
    new->next = NULL;
    new->prev = NULL;
    new->buf = buf;
    new->key = key;
    new->ref = 0;
    new->size = buf_size;

    // check to see if cache is empty, if so add new
    if (cache->start == NULL && cache->end == NULL) {
        cache->start = new;
        cache->end = new;
    } else { // cache is nonempty, add to start of the cache
        new->next = cache->start;
        cache->start->prev = new;
        cache->start = new;
    }

    // increase size of data stored by cache
    cache->size += buf_size;
}

/* Initializes the cache object
 *
 * Must be called before any other function is called
 */
void cache_init() {
    // create cache object
    cache = Malloc(sizeof(cache_t));
    cache->start = NULL;
    cache->end = NULL;
    cache->size = 0;
}
