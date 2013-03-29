// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_STATS_H_
#define WHEATSERVER_STATS_H_

#define WHEAT_STATCOMMAND_PACKET_FIELD       9

#define ONLY_MASTER                          (1)

#define getStatValByName(name)               (getStatItemByName(name)->val)
#define getStatVal(stat)                     ((stat)->val)

enum statType {
    SUM_STAT,
    MAX_STAT,
    ASSIGN_STAT,
};

enum statPrintFormat {
    RAW,
    MICORSECONDS_TIME,
    LOCAL_TIME,
};

struct statItem {
    char *name;
    enum statType type;
    enum statPrintFormat format;
    int flags;
    long long val;
};

struct masterClient;
struct workerProcess;

struct statItem *getStatItemByName(const char *name);
void sendStatPacket(struct workerProcess *worker_process);
void logStat();
void statinputCommand(struct masterClient *c);
void statCommand(struct masterClient *c);
void initServerStats(struct array *confs);

#endif
