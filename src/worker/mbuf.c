// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../debug.h"
#include "../slice.h"
#include "mbuf.h"
#include "../memalloc.h"

#define WHEAT_MBUF_MAGIC       0x19920828

/*
 * mbuf header is at the tail end of the mbuf. This enables us to catch
 * buffer overrun early by asserting on the magic value during get or
 * put operations
 *
 *   <------------- mbuf_chunk_size ------------->
 *   +-------------------------------------------+
 *   |       mbuf data          |  mbuf header   |
 *   |     (mbuf_offset)        | (struct mbuf)  |
 *   +-------------------------------------------+
 *   ^           ^        ^     ^^
 *   |           |        |     ||
 *   \           |        |     |\
 *   mbuf->start \        |     | mbuf->end (one byte past valid bound)
 *                mbuf->protect_pos\
 *                        \      mbuf
 *                        mbuf->write_pos (one byte past valid byte)
 *
 */

struct msghdr {
    struct mbuf *last_write;
    struct mbuf *protected;
    struct mbuf *last_read;
    size_t mbuf_len;
    size_t mbuf_size;
    uint8_t is_set_writted_after_put;
    uint8_t is_set_readed_after_read;
};

struct mbuf {
    uint32_t magic;
    struct mbuf *next;
    uint8_t *read_pos;
    uint8_t *write_pos;
    uint8_t *start;
    uint8_t *end;
};


static struct mbuf *mbufGet(size_t mbuf_size)
{
    struct mbuf *mbuf;
    // extra one is left as mbuf->end
    uint8_t *m = wmalloc(sizeof(*mbuf)+mbuf_size+1);
    if (m == NULL)
        return NULL;
    mbuf = (struct mbuf *)(m + mbuf_size + 1);
    mbuf->end = m + mbuf_size;
    mbuf->read_pos = mbuf->write_pos = mbuf->start = m;
    mbuf->magic = WHEAT_MBUF_MAGIC;
    mbuf->next = NULL;
    return mbuf;
}

static void mbufFree(struct mbuf *mbuf, size_t mbuf_size)
{
    uint8_t *m = (uint8_t *)mbuf - mbuf_size - 1;
    wfree(m);
}

struct msghdr *msgCreate(size_t mbuf_size)
{
    struct mbuf *mbuf = mbufGet(mbuf_size);
    if (mbuf == NULL)
        return NULL;
    struct msghdr *hdr = wmalloc(sizeof(*hdr));
    if (hdr == NULL) {
        mbufFree(mbuf, mbuf_size);
        return NULL;
    }
    hdr->last_read = hdr->last_write = hdr->protected = mbuf;
    hdr->mbuf_len = 1;
    hdr->mbuf_size = mbuf_size;
    hdr->is_set_writted_after_put = hdr->is_set_readed_after_read =  1;
    return hdr;
}

void msgClean(struct msghdr *hdr)
{
    struct mbuf *next, *curr = hdr->protected;
    size_t mbuf_size = hdr->mbuf_size;
    while (curr && curr != hdr->last_read) {
        next = curr->next;
        mbufFree(curr, mbuf_size);
        curr = next;
        hdr->mbuf_len--;
    }
    hdr->protected = curr;
}

void msgRead(struct msghdr *hdr, struct slice *s)
{
    assert(hdr && s);
    assert(hdr->is_set_readed_after_read == 1);
    struct mbuf *mbuf = hdr->last_read;
    assert(mbuf->magic == WHEAT_MBUF_MAGIC);
    // If hdr->last_read is read over but next mbuf exists
    if (mbuf->read_pos == mbuf->write_pos && mbuf->write_pos == mbuf->end &&
            mbuf->next != NULL) {
        mbuf = mbuf->next;
    }
    sliceTo(s, mbuf->read_pos, mbuf->write_pos - mbuf->read_pos);
    hdr->last_read = mbuf;
    hdr->is_set_readed_after_read = 0;
}

int msgPut(struct msghdr *hdr, struct slice *s)
{
    assert(hdr && s);
    assert(hdr->is_set_writted_after_put == 1);

    struct mbuf *mbuf = hdr->last_write;
    assert(mbuf->magic == WHEAT_MBUF_MAGIC);
    if (mbuf->write_pos == mbuf->end) {
        struct mbuf *old_buf = mbuf;
        mbuf = mbufGet(hdr->mbuf_size);
        if (mbuf == NULL)
            return -1;
        old_buf->next = mbuf;
        hdr->mbuf_len++;
        hdr->last_write = mbuf;
    }
    sliceTo(s, mbuf->write_pos, mbuf->end - mbuf->write_pos);
    hdr->is_set_writted_after_put = 0;
    return 0;
}

void msgFree(struct msghdr *hdr)
{
    struct mbuf *next, *curr = hdr->protected;
    while (curr != NULL) {
        next = curr->next;
        mbufFree(curr, hdr->mbuf_size);
        curr = next;
        hdr->mbuf_len--;
    }
    wfree(hdr);
}

void msgSetReaded(struct msghdr *hdr, size_t len)
{
    assert(hdr->last_read->magic == WHEAT_MBUF_MAGIC);
    assert(len <= hdr->last_read->write_pos - hdr->last_read->read_pos);
    assert(hdr->is_set_readed_after_read == 0);
    hdr->last_read->read_pos += len;
    hdr->is_set_readed_after_read = 1;
}

void msgSetWritted(struct msghdr *hdr, size_t len)
{
    assert(hdr->last_write->magic == WHEAT_MBUF_MAGIC);
    assert(len <= hdr->last_write->end - hdr->last_write->write_pos);
    assert(hdr->is_set_writted_after_put == 0);
    hdr->last_write->write_pos += len;
    hdr->is_set_writted_after_put = 1;
}

size_t msgGetSize(struct msghdr *hdr)
{
    return hdr->mbuf_len * hdr->mbuf_size;
}

int msgCanRead(struct msghdr *hdr)
{
    struct mbuf *buf = hdr->last_read;
    assert(buf->magic == WHEAT_MBUF_MAGIC);
    return (buf->read_pos != buf->write_pos) ||
        (buf->next != NULL && buf->next->write_pos != buf->next->start);
}

#ifdef MBUF_TEST_MAIN
#include <stdio.h>
#include "../test_help.h"

int main(int argc, const char *argv[])
{
    {
        size_t mbuf_size = 512;
        struct msghdr *hdr = msgCreate(mbuf_size);
        struct slice slice;
        int ret = msgPut(hdr, &slice);
        test_cond("msg put", ret == 0);
        test_cond("msg put", slice.len == mbuf_size);
        char buf[255];
        int i = 0;
        for (; i < 255; ++i)
            buf[i] = 'a';
        memcpy(slice.data, buf, 255);
        msgSetWritted(hdr, 255);
        msgRead(hdr, &slice);
        test_cond("msg get", slice.len == 255);
        msgSetReaded(hdr, 255);
        msgRead(hdr, &slice);
        test_cond("msg get", slice.len == 0);
        msgSetReaded(hdr, slice.len);
        ret = msgPut(hdr, &slice);
        test_cond("msg put", ret == 0 && slice.len == mbuf_size-255);
        msgSetWritted(hdr, mbuf_size-255);
        msgRead(hdr, &slice);
        msgSetReaded(hdr, slice.len);
        test_cond("msg get", ret == 0 && slice.len == mbuf_size-255);
        ret = msgPut(hdr, &slice);
        test_cond("msg put", ret == 0 && slice.len == mbuf_size);
        msgSetWritted(hdr, 255);
        msgRead(hdr, &slice);
        msgSetReaded(hdr, slice.len);
        test_cond("msg get", ret == 0 && slice.len == 255);
        msgClean(hdr);
        test_cond("msg clean", ret == 0 && hdr->mbuf_len == 1);
        msgFree(hdr);
    }
    test_report();
    return 0;
}

#endif
