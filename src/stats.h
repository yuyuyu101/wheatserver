// Statistic module - implementation of send statistic packet and revevant
// utils
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_STATS_H_
#define WHEATSERVER_STATS_H_

// Gather statistic example:
//     static struct statItem *stat_item = NULL;
//     static long long *stat_item_val = 0;
//
//     void XXXXSetup()
//     {
//         stat_item = getStatItemByName("stat item");
//         stat_item_val = &getStatValByName("stat item val");
//     }
//
//     void appCron()
//     {
//         getStatVal(stat_item)++;
//         *stat_item_val++;
//
//  OR
//         getStatVal(stat_item) = some_val;
//         *stat_item_val = some_val;
//     }
//
// You can cache stat item on your module in order to reduce search stat item
// by name every time. Via referencing `value` or statItem both are OK.
//
// Statistic note:
// Worker process will gather changes on statistic fields and send changes
// to master process every heart cron.

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

// You can define your statistic item in your module.
// `name`: statistic field name, displayed in statistic report
// `type`: use type below listed. You can get exact purpose from StatItems in
// stats.c
//     1. ASSIGN_STAT: statistic value is assigned to master aggregation
//     2. SUM_STAT: statistic value is the sum of all value gathered
//     3. MAX_STAT: statistic value is set the max value of all value gathered
// `format`: statistic value formatted type, you can specify below listed.
//     1. LOCAL_TIME
//     2. MICORSECONDS_TIME
//     3. RAW
// `flags`: flags indicate stat item
//     * ONLY_MASTER: this statistic field is ignored when worker send statistic
//     packet to master process. In other words, this field is only used master
//     process.
// `val`: the actual place storing message
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
