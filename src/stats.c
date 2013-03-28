// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

 #include "wheatserver.h"

// *Attention*: You shouldn't change old StatItems order, id and name,
// because some code directly use offset of StatItems to get statItem
// or by name
struct statItem StatItems[] = {
    {0, "Last send", ASSIGN_STAT, LOCAL_TIME, 0, 0},
    {1, "Total spawn workers", SUM_STAT, RAW, ONLY_MASTER, 0},
    {2, "Timeout workers", SUM_STAT, RAW, ONLY_MASTER, 0},
    {3, "Total client", SUM_STAT, RAW, 0, 0},
    {4, "Total request", SUM_STAT, RAW, 0, 0},
    {5, "Total timeout client", SUM_STAT, RAW, 0, 0},
    {6, "Total failed request", SUM_STAT, RAW, 0, 0},
    {7, "Max buffer size", MAX_STAT, RAW, 0, 0},
    {8, "Worker run time", SUM_STAT, MICORSECONDS_TIME, 0, 0},
    {9, "Max worker cron interval", ASSIGN_STAT, RAW, 0, 0},
};

#define STAT_ITEMS_COUNT (sizeof(StatItems)/sizeof(struct statItem))

struct statItem *getStatItemByName(const char *name)
{
    size_t count = STAT_ITEMS_COUNT;
    struct statItem *stat = StatItems;
    while (count--) {
        if (!strcmp(name, stat->name))
            return stat;
        stat++;
    }
    ASSERT(0);
    return NULL;
}

/* ========== Worker Statistic Area ========== */

static int connectWithMaster(struct workerProcess *worker_process)
{
    int fd;

    fd = wheatTcpConnect(Server.neterr, Server.stat_addr, Server.stat_port);
    if (fd == NET_WRONG) {
        wheatLog(WHEAT_WARNING,"Unable to connect to MASTER: %s %s",
            strerror(errno), Server.neterr);
        return WHEAT_WRONG;
    }
    if (worker_process->master_stat_fd != 0) {
        close(worker_process->master_stat_fd);
        wheatLog(WHEAT_WARNING,"build new connection to MASTER: %s %s");
    }
    worker_process->master_stat_fd = fd;

    return WHEAT_OK;
}

static wstr defaultStatCommand(struct workerProcess *worker_process)
{
    char buf[255];
    snprintf(buf, 255, "\r\rSTATINPUT\n%d", worker_process->pid);
    wstr out = wstrNew(buf);
    return out;
}

void sendStatPacket(struct workerProcess *worker_process)
{
    struct statItem *stat = worker_process->stat;
    if (worker_process->master_stat_fd == 0) {
        int ret;
        ret = connectWithMaster(worker_process);
        if (ret == WHEAT_WRONG)
            return ;
    }
    char buf[WHEAT_STAT_PACKET_MAX];
    ssize_t ret, nwrite;
    size_t count;
    struct slice s;
    wstr stat_packet = defaultStatCommand(worker_process);
    count = STAT_ITEMS_COUNT;
    while (count--) {
        // If this statItem's val is zero or this statItem only used be in
        // master, skip it
        if (stat->val != 0 && !(stat->flags & ONLY_MASTER)) {
            ret = snprintf(buf, WHEAT_STAT_PACKET_MAX, "\n%d\n%lld",
                    stat->id, stat->val);
            if (ret < 0 || ret > WHEAT_STAT_PACKET_MAX) {
                wstrFree(stat_packet);
                return ;
            }

            stat_packet = wstrCatLen(stat_packet, buf, ret);
            // Avoid set last send field to zero because of this send may failed
            if (stat->id != 0)
                stat->val = 0;
        }
        stat++;
    }
    // No stat need to send
    stat_packet = wstrCatLen(stat_packet, "$", 1);
    sliceTo(&s, (uint8_t *)stat_packet, wstrlen(stat_packet));
    nwrite = writeBulkTo(worker_process->master_stat_fd, &s);
    wstrFree(stat_packet);
    if (nwrite == -1) {
        wheatLog(WHEAT_DEBUG, "Master close connection fd:%d",
                worker_process->master_stat_fd);
        worker_process->master_stat_fd = 0;
    } else if (nwrite != s.len) {
        wheatLog(WHEAT_DEBUG,
                "send statistic info failed, total %d sended %d",
                s.len, nwrite);
    } else {
        worker_process->stat->val = Server.cron_time.tv_sec;
    }
}

/* ========== Master Statistic Area ========== */

struct statItem *copyStatItems()
{
    size_t len = STAT_ITEMS_COUNT * sizeof(struct statItem);
    uint8_t *p = wmalloc(len);
    memcpy(p, StatItems, len);
    return (struct statItem *)p;
}

static ssize_t parseStat(struct masterClient *client,
        struct workerProcess **owner)
{
    pid_t pid;
    struct listNode *node = NULL;
    struct workerProcess *worker = NULL;
    struct listIterator *iter;
    int i, stat_id, ret;
    long long val;
    struct statItem *server_stat = Server.aggregate_stat;
    if (client->argc < 2)
        return WHEAT_WRONG;
    pid = atoi(client->argv[1]);
    iter = listGetIterator(Server.workers, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        worker = listNodeValue(node);
        if (worker->pid == pid)
            break;
    }
    freeListIterator(iter);
    if (!worker) return WHEAT_WRONG;
    *owner = worker;
    worker->refresh_time = Server.cron_time.tv_sec;

    if (client->argc % 2 != 0)
        return WHEAT_WRONG;

    for (i = 2; i < client->argc; i += 2) {
        stat_id = atoi(client->argv[i]);
        ret = string2ll(client->argv[i+1], wstrlen(client->argv[i+1]), &val);
        if (ret == WHEAT_WRONG || stat_id >= STAT_ITEMS_COUNT)
            return WHEAT_WRONG;
        switch(server_stat[stat_id].type) {
            case SUM_STAT:
                worker->stat[stat_id].val += val;
                server_stat[stat_id].val += val;
                break;
            case MAX_STAT:
                if (worker->stat[stat_id].val < val)
                    worker->stat[stat_id].val = val;
                if (server_stat[stat_id].val < val)
                    server_stat[stat_id].val = val;
                break;
            case ASSIGN_STAT:
                worker->stat[stat_id].val = val;
                server_stat[stat_id].val = val;
                break;
        }
    }
    return WHEAT_OK;
}

void statinputCommand(struct masterClient *client)
{
    wstr buf = client->request_buf;
    struct workerProcess *worker;
    ssize_t parse_ret;
    parse_ret = parseStat(client, &worker);
    if (parse_ret == WHEAT_OK) {
        wheatLog(WHEAT_DEBUG, "receive worker statistic info %d", client->fd);
    }
    else {
        wheatLog(WHEAT_DEBUG, "parse stat packet failed: %s",
                buf);
    }
}

static wstr getStatFormat(struct statItem *stat_items, wstr format_stat)
{
    int ret, i = 0;
    char buf[255];
    long long print_val;
    do {
        switch (stat_items[i].format) {
            case MICORSECONDS_TIME:
                print_val = stat_items[i].val / 1000000;
                ret = snprintf(buf, 255, "%s: %llds\n", stat_items[i].name, print_val);
                break;
            case LOCAL_TIME:
                ret = snprintf(buf, 255, "%s: %s", stat_items[i].name,
                        ctime((time_t*)&stat_items[i].val));
                break;
            case RAW:
                ret = snprintf(buf, 255, "%s: %lld\n", stat_items[i].name, stat_items[i].val);
                break;
        }
        format_stat = wstrCatLen(format_stat, buf, ret);
    } while (++i < STAT_ITEMS_COUNT);
    return format_stat;
}

void logStat()
{
    wstr format_stat = wstrEmpty();
    wheatLog(WHEAT_LOG_RAW, "---- Master Statistic Information -----\n");
    format_stat = getStatFormat(Server.aggregate_stat, format_stat);
    wheatLog(WHEAT_LOG_RAW, "%s", format_stat);
//    wheatLog(WHEAT_LOG_RAW, "-- Workers Statistic Information are --\n");
//    struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
//    while ((node = listNext(iter)) != NULL) {
//        worker = listNodeValue(node);
//        stat = worker->stat;
//        wheatLog(WHEAT_LOG_RAW, "\nStart Time: %s", ctime(&worker->start_time));
//        getWorkerStatFormat(stat, buf, 1024);
//        wheatLog(WHEAT_LOG_RAW, "%s", buf);
//    }
//    freeListIterator(iter);
    wheatLog(WHEAT_LOG_RAW, "---------------------------------------\n");
    wstrFree(format_stat);
}

void statCommand(struct masterClient *c)
{
    wstr format_stat = wstrEmpty();
    if (!wstrCmpNocaseChars(c->argv[1], "master", 6)) {
        format_stat = getStatFormat(Server.aggregate_stat, format_stat);
    } else if (!wstrCmpNocaseChars(c->argv[1], "worker", 6)) {
        struct listNode *node = NULL;
        struct workerProcess *worker = NULL;
        struct listIterator *iter;
        iter = listGetIterator(Server.workers, START_HEAD);
        while ((node = listNext(iter)) != NULL) {
            worker = listNodeValue(node);
            format_stat = getStatFormat(worker->stat, format_stat);
        }
        freeListIterator(iter);
    }
    replyMasterClient(c, format_stat, wstrlen(format_stat));
    wstrFree(format_stat);
}
