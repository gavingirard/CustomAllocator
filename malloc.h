#ifndef _MALLOC_H
#define _MALLOC_H

#include <stddef.h>

#define malloc(size) custom_malloc(size)
#define calloc(nmemb, size) custom_calloc(nmemb, size)
#define free(ptr) custom_free(ptr)

void *custom_malloc(size_t size);
void *custom_calloc(size_t nmemb, size_t size);
void custom_free(void *ptr);

#endif /* ifndef _MALLOC_H */
