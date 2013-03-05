#ifndef WHEATSERVER_SLICES_H
#define WHEATSERVER_SLICES_H

struct slice {
    const char *data;
    size_t len;
};

struct slice *sliceCreate(const char *data, size_t len);
void sliceFree(struct slice *b);
void sliceClear(struct slice *b);
void sliceRemvoePrefix(struct slice *b, size_t prefix);
int sliceStartWith(struct slice *b, struct slice *s);
int sliceCompare(struct slice *b, struct slice *s);

#endif
