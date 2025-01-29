# Custom Memory Allocator

Custom implementation of `malloc()`, `calloc()`, `realloc()`, and `free()`.

### Notes / Assumptions / Choices
- All functions behave identically to the normal C `<stdlib.h>` functions
- Uses an embedded linked list data structure where each allocation has its own node in the list containing its size and the next item
- Each block header has a magic number inside of it to make sure that all headers are intact and are not overwritten
- Does not align user memory to 8 or 16 bytes although this would be a good future improvement
- Freeing blocks does not overwrite or zero out sections of memory
- Does not detect double frees or have any kind of support for valgrind
- First fit allocation is easy but isn't super efficient and is prone to fragmentation which definitely could happen here if you allocate and free tons of weird memory sizes

Despite its limitations, this was a really fun program to write and I think is a great exercise in learning about how the actual native C functions work.
