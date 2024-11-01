#define _DEFAULT_SOURCE
#define _BSD_SOURCE 
#include <stdio.h> 
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "malloc.h"

#ifdef MEM_DEBUG
#define debug_printf(...) fprintf(stderr, "[DEBUG] - " __VA_ARGS__)
#else
#define debug_printf(...)
#endif

// Magic number for headers to ensure that the heap isn't corrupted (this is a uint_32t)
#define MAGIC 0xDEADC0DE

// Pointers which keep track of the program's heap space
char *heap_start = NULL;
char *heap_end = NULL;

// Header which sits right before a block of user memory
typedef struct header {
  size_t dsize;
  uint32_t magic;
  struct header *next;
} header_t;

// Insert a new block into the heap between the two given header pointers given the address of the
// new block and the size of its data region
void insert_block(char *addr, size_t dsize, header_t *prev, header_t *next) {
  assert(addr != NULL);
  assert(heap_start != NULL);

  header_t *block = (header_t *) addr;
  block->dsize = dsize;
  block->magic = MAGIC;
  block->next = next;
  if (prev != NULL) {
    prev->next = block;
  }
}

// Initialize the start of the heap given the block size of the first item. This takes in the size
// of the first item so that the dummy element does not use up a sbrk call. This function is only 
// called once in an app's lifetime
void init_heap(size_t bsize) {
  size_t initial_size = sizeof(header_t) + bsize;
  heap_start = sbrk((int) initial_size);
  if (heap_start == (void *) -1) {
    heap_start = NULL;
  } else {
    // Create the dummy head element at the start of the heap
    insert_block(heap_start, 0, NULL, NULL);
    heap_end = heap_start + initial_size;
  }
}

// Given the start of a new block to go at the end of the heap and the size of the block to be
// added, extend the heap if necessary. Returns 0 on success and -1 on failure
int expand_heap(char *block_start, size_t bsize) {
  assert(block_start != NULL);
  assert(heap_start != NULL);

  char *block_end = block_start + bsize;
  // If the end of the block is past the end of the heap then we need to expand
  if (block_end > heap_end) {
    size_t expansion = (size_t) (block_end - heap_end);
    void *res = sbrk((int) expansion);
    if (res == (void *) -1) {
      return -1;
    }
    heap_end += expansion;
  }
  return 0;
}

// Check if the given header pointer is valid by checking its location and magic number. NULL is
// special here since it will cause any for loops to break, so a NULL header is valid
bool valid_header(header_t *hptr) {
  if (hptr == NULL) {
    return true;
  }
  // Is the pointer between the start of the heap and the last possible place a header could go?
  if ((char *) hptr >= heap_start && (char *) hptr <= heap_end - sizeof(header_t)) {
    return hptr->magic == MAGIC;
  }
  return false;
}

// Find the first area in the heap that is large enough to accomodate the block size requested.
// Returns the pointer to the start of this block, and sets prev to the address of the previous
// header and next to the address of the next header, if it exists
char *find_opening(size_t bsize, header_t **prev, header_t **next) {
  assert(heap_start != NULL);

  header_t *curr = (header_t *) heap_start;
  for (;;) {
    // Get the size of the current block
    size_t curr_block_size = sizeof(header_t) + curr->dsize;
    // If the current block has no next block, we didn't find a large enough opening so we will
    // just add it to the end of the heap
    if (curr->next == NULL) {
      *prev = curr;
      *next = NULL;
      return (char *) curr + curr_block_size;
    }
    char *curr_block_end = (char *) curr + curr_block_size;
    size_t open_space = (size_t) ((char *) curr->next - curr_block_end);
    // If the space is large enough to fit the block, return the address of the start of the header
    // for the new block
    if (open_space >= bsize) {
      *prev = curr;
      *next = curr->next;
      return curr_block_end;
    }
    curr = curr->next;
    if (!valid_header(curr)) {
      return NULL;
    }
  }
}

// Return true if there is space for the given header to expand its data area to the given size
// before the start of the next header
bool can_expand(header_t *block, size_t s) {
  assert(block != NULL);
  assert(heap_start != NULL);

  char *new_block_end = (char *) block + sizeof(header_t) + s;
  if (block->next == NULL) {
    // If the block is at the end of the heap then expansion might be needed
    expand_heap((char *) block, s + sizeof(header_t));
    return true;
  } else {
    char *next_block_start = (char *) block->next;
    return (size_t) (next_block_start - new_block_end) >= 0;
  }
}

// Find the block of memory given its header pointer, setting the address of the previous block if
// found. Returns NULL if the block does not exist, or -1 if the heap is corrupted
header_t *find_block(header_t *target, header_t **prev) {
  assert(heap_start != NULL);
  assert(target != NULL);

  header_t *curr = (header_t *) heap_start;
  while (curr != NULL) {
    // If the current block is the target pointer, we found the correct block
    if (curr == target) {
      return curr;
    }
    *prev = curr;
    curr = curr->next;
    if (!valid_header(curr)) {
      return (void *) -1;
    }
  }
  return NULL;
}

// Allocate a new block of memory with a size of s bytes. Returns a pointer to the newly allocated
// space in memory, or NULL if the allocation failed
void *custom_malloc(size_t s) {
  // Size of the region needed to store this allocation
  size_t bsize = sizeof(header_t) + s;
  // If the heap hasn't been used before, initialize it
  bool just_initialized = false;
  if (heap_start == NULL) {
    init_heap(bsize);
    just_initialized = true;
    if (heap_start == NULL) {
      debug_printf("Malloc 0 bytes (Heap initialization failed)\n");
      return NULL;
    }
  }
  header_t *prev, *next;
  char *malloc_block_start = find_opening(bsize, &prev, &next);
  // If something went wrong while looking through the heap
  if (malloc_block_start == NULL) {
    debug_printf("Malloc 0 bytes (Heap corrupted)\n");
    return NULL;
  }
  // If we need to add the block to the end of the heap and it wasn't already added by init_heap, 
  // try to extend the heap to accomodate the new block
  if (next == NULL && !just_initialized) {
    if (expand_heap(malloc_block_start, bsize) == -1) {
      debug_printf("Malloc 0 bytes (Heap expansion failed)\n");
      return NULL;
    }
  }
  // Insert this new block into the linked list
  insert_block(malloc_block_start, s, prev, next);
  debug_printf("Malloc %zu bytes\n", s);
  // Return the start of the block plus the header size to get the user's data
  return (void *) (malloc_block_start + sizeof(header_t));
}

// Reallocate the block of memory at the given pointer to a new size. Returns a pointer to the
// newly allocated space in memory which might not necessarily be the same as the old pointer, or
// NULL if the allocation failed (in which case the old pointer is still valid)
void *custom_realloc(void *p, size_t s) {
  // If the pointer is NULL, just malloc the new size
  if (p == NULL) {
    return custom_malloc(s);
  }
  // If the size is zero, free the old pointer and return NULL
  if (s == 0) {
    custom_free(p);
    return NULL;
  }
  // Get the header of the old block
  header_t *prev;
  header_t *target = (header_t *) ((char *) p - sizeof(header_t));
  header_t *old = find_block(target, &prev);
  // If the old block was not found, return NULL
  if (old == NULL) {
    debug_printf("Realloc 0 to 0 bytes (Invalid pointer)\n");
    return NULL;
  } else if (old == (void *) -1) {
    debug_printf("Realloc 0 to 0 bytes (Heap corrupted)\n");
    return NULL;
  }
  if (can_expand(old, s)) {
    // If the block can be expanded, update the size and return the pointer
    debug_printf("Realloc %zu to %zu bytes\n", old->dsize, s);
    old->dsize = s;
    char *data = (char *) old + sizeof(header_t);
    return (void *) data;
  } else {
    // Otherwise allocate a new block and copy the data over, then free the old pointer
    void *new = custom_malloc(s);
    if (new == NULL) {
      debug_printf("Realloc 0 to 0 bytes (Allocation failed)\n");
      return NULL;
    }
    memcpy(new, p, old->dsize);
    custom_free(p);
    debug_printf("Realloc %zu to %zu bytes\n", old->dsize, s);
    return new;
  }
}

// Allocate and initialize memory for an array of nmemb elements of size s bytes with all values 
// set to zero. Returns a pointer to the newly allocated space in memory, or NULL if the allocation
// failed or if the size of an element or the size of the array were equal to zero
void *custom_calloc(size_t nmemb, size_t s) {
  // If the array's size or the element's size is equal to zero return NULL
  if (nmemb == 0 || s == 0) {
    debug_printf("Calloc 0 bytes (Invalid size)\n");
    return NULL;
  }
  size_t size = nmemb * s;
  void *array = custom_malloc(size);
  if (array == NULL) {
    debug_printf("Calloc 0 bytes (Allocation failed)\n");
    return NULL;
  }
  memset(array, 0, size);
  debug_printf("Calloc %zu bytes\n", size);
  return array;
}

// Free the block of memory at the given pointer. If the pointer is NULL or an invalid region of
// memory is freed, it will not do anything
void custom_free(void *p) {
  // If the pointer is NULL, the list head was freed, or the heap is uninitialized, do nothing
  if (p == NULL) {
    debug_printf("Freed 0 bytes\n");
    return;
  }
  if (heap_start == NULL || (char *) p - sizeof(header_t) == heap_start) {
    debug_printf("Freed 0 bytes (Invalid pointer)\n");
    return;
  }
  header_t *prev;
  header_t *target = (header_t *) ((char *) p - sizeof(header_t));
  header_t *block = find_block(target, &prev);
  // Free the block if it was found and is valid
  if (block == NULL) {
    debug_printf("Freed 0 bytes (Invalid pointer)\n");
  } else if (block == (void *) -1) {
    debug_printf("Freed 0 bytes (Heap corrupted)\n");
  } else {
    prev->next = block->next;
    debug_printf("Freed %zu bytes\n", block->dsize);
  }
}
