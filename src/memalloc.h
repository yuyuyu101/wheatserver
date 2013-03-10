#ifndef WHEATSERVER_MEMALLOC
#define WHEATSERVER_MEMALLOC

void *wmalloc(size_t size);
void *wrealloc(void *old, size_t size);
void *wcalloc(size_t count, size_t size);
void wfree(void *ptr);

#endif
