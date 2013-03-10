#ifndef WHEATSERVER_SLAB_H
#define WHEATSERVER_SLAB_H

struct slabcenter;
struct slabcenter *slabcenterCreate(const int item_max, const double factor);
void slabcenterDealloc(struct slabcenter *c);

void *slabAlloc(struct slabcenter *center, const size_t size);

#endif
