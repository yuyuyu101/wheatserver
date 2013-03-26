// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_PROTOCOL_REDIS_PROTO_REDIS_H
#define WHEATSERVER_PROTOCOL_REDIS_PROTO_REDIS_H

struct slice *redisBodyNext(struct conn *c);
void redisBodyStart(struct conn*c);
void getRedisKey(struct conn *c, struct slice *out);
int isReadCommand(struct conn*);


#define str3icmp(m, c0, c1, c2)                                                             \
    ((m[0] == c0 || m[0] == (c0 ^ 0x20)) &&                                                 \
     (m[1] == c1 || m[1] == (c1 ^ 0x20)) &&                                                 \
     (m[2] == c2 || m[2] == (c2 ^ 0x20)))

#define str4icmp(m, c0, c1, c2, c3)                                                         \
    (str3icmp(m, c0, c1, c2) && (m[3] == c3 || m[3] == (c3 ^ 0x20)))

#define str5icmp(m, c0, c1, c2, c3, c4)                                                     \
    (str4icmp(m, c0, c1, c2, c3) && (m[4] == c4 || m[4] == (c4 ^ 0x20)))

#define str6icmp(m, c0, c1, c2, c3, c4, c5)                                                 \
    (str5icmp(m, c0, c1, c2, c3, c4) && (m[5] == c5 || m[5] == (c5 ^ 0x20)))

#define str7icmp(m, c0, c1, c2, c3, c4, c5, c6)                                             \
    (str6icmp(m, c0, c1, c2, c3, c4, c5) &&                                                 \
     (m[6] == c6 || m[6] == (c6 ^ 0x20)))

#define str8icmp(m, c0, c1, c2, c3, c4, c5, c6, c7)                                         \
    (str7icmp(m, c0, c1, c2, c3, c4, c5, c6) &&                                             \
     (m[7] == c7 || m[7] == (c7 ^ 0x20)))

#define str9icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8)                                     \
    (str8icmp(m, c0, c1, c2, c3, c4, c5, c6, c7) &&                                         \
     (m[8] == c8 || m[8] == (c8 ^ 0x20)))

#define str10icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9)                                \
    (str9icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8) &&                                     \
     (m[9] == c9 || m[9] == (c9 ^ 0x20)))

#define str11icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10)                           \
    (str10icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9) &&                                \
     (m[10] == c10 || m[10] == (c10 ^ 0x20)))

#define str12icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11)                      \
    (str11icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10) &&                           \
     (m[11] == c11 || m[11] == (c11 ^ 0x20)))

#define str13icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12)                 \
    (str12icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11) &&                      \
     (m[12] == c12 || m[12] == (c12 ^ 0x20)))

#define str14icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13)            \
    (str13icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12) &&                 \
     (m[13] == c13 || m[13] == (c13 ^ 0x20)))

#define str15icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14)       \
    (str14icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13) &&            \
     (m[14] == c14 || m[14] == (c14 ^ 0x20)))

#define str16icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14, c15)  \
    (str15icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14) &&       \
     (m[15] == c15 || m[15] == (c15 ^ 0x20)))

#endif
