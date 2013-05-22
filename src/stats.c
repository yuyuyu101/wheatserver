// Statistic module - implementation of send statistic packet and revevant
// utils
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

 #include "wheatserver.h"

// *Attention*: You shouldn't change old StatItems order, id and name,
// because some code directly use offset of StatItems to get statItem
// or by name
static struct statItem StatItems[] = {
    {"Last send", ASSIGN_STAT, LOCAL_TIME, 0, 0},
    {"Total spawn workers", SUM_STAT, RAW, ONLY_MASTER, 0},
    {"Timeout workers", SUM_STAT, RAW, ONLY_MASTER, 0},
    {"Total client", SUM_STAT, RAW, 0, 0},
    {"Total request", SUM_STAT, RAW, 0, 0},
    {"Total timeout client", SUM_STAT, RAW, 0, 0},
    {"Total failed request", SUM_STAT, RAW, 0, 0},
    {"Max buffer size", MAX_STAT, RAW, 0, 0},
    {"Worker run time", SUM_STAT, MICORSECONDS_TIME, 0, 0},
    {"Max worker cron interval", ASSIGN_STAT, RAW, 0, 0},
    {"Max memory usage", MAX_STAT, RAW, 0, 0},
};

struct statItem *getStatItemByName(const char *name)
{
    struct statItem *stat;
    struct array *stats;
    int i;

    stats = Server.stats;
    for (i = 0; i < narray(stats); ++i) {
        stat = arrayIndex(stats, i);
        if (!strcmp(name, stat->name))
            return stat;
    }
    wheatLog(WHEAT_WARNING, "no matched %s", name);
    return NULL;
}

// Push StatItems to stats.
void initServerStats(struct array *stats)
{
    struct statItem *stat;
    int len, i;

    len = sizeof(StatItems) / sizeof(struct statItem);
    i = 0;
    while (i != len) {
        stat = &StatItems[i];
        arrayPush(stats, stat);
        i++;
    }
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

// We only send statistic fields not equal to zero(changed).
// Statistic packet format:
// "\r\rSTATINPUT\nfield\nvalue\nfield\nvalue\nfield\nvalue$"
// "\r\r" is the start indicator and "$" is end indicator
void sendStatPacket(struct workerProcess *worker_process)
{
    char buf[WHEAT_STAT_PACKET_MAX];
    ssize_t ret, nwrite;
    size_t count, pos;
    struct slice s;
    struct statItem *stat;

    // Connect to master
    if (worker_process->master_stat_fd == 0) {
        ret = connectWithMaster(worker_process);
        if (ret == WHEAT_WRONG)
            return ;
    }

    stat = arrayData(Server.stats);
    count = narray(Server.stats);
    pos = 0;
    wstr stat_packet = defaultStatCommand(worker_process);
    while (pos < count) {
        // If this statItem's val is zero or this statItem only used be in
        // master, skip it
        if (stat->val != 0 && !(stat->flags & ONLY_MASTER)) {
            ret = snprintf(buf, WHEAT_STAT_PACKET_MAX, "\n%ld\n%lld",
                    pos, stat->val);
            if (ret < 0 || ret > WHEAT_STAT_PACKET_MAX) {
                wstrFree(stat_packet);
                return ;
            }

            stat_packet = wstrCatLen(stat_packet, buf, ret);
            // Avoid set last send field to zero because of this send may failed
            if (pos != 0)
                stat->val = 0;
        }
        pos++;
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
        stat = arrayData(Server.stats);
        stat->val = Server.cron_time.tv_sec;
    }
}

/* ========== Master Statistic Area ========== */

// Parse statistic packet from worker, apply changes to `Server` variable
static ssize_t parseStat(struct masterClient *client,
        struct workerProcess **owner)
{
    pid_t pid;
    struct listNode *node = NULL;
    struct workerProcess *worker = NULL;
    struct listIterator *iter;
    int i, stat_id, ret;
    size_t count;
    long long val;
    struct statItem *server_stat, *worker_stat;

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

    server_stat = arrayData(Server.stats);
    worker_stat = arrayData(worker->stats);
    count = narray(Server.stats);
    for (i = 2; i < client->argc; i += 2) {
        stat_id = atoi(client->argv[i]);
        ret = string2ll(client->argv[i+1], wstrlen(client->argv[i+1]), &val);
        if (ret == WHEAT_WRONG || stat_id >= count)
            return WHEAT_WRONG;
        switch(server_stat[stat_id].type) {
            case SUM_STAT:
                worker_stat[stat_id].val += val;
                server_stat[stat_id].val += val;
                break;
            case MAX_STAT:
                if (worker_stat[stat_id].val < val)
                    worker_stat[stat_id].val = val;
                if (server_stat[stat_id].val < val)
                    server_stat[stat_id].val = val;
                break;
            case ASSIGN_STAT:
                worker_stat[stat_id].val = val;
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

static wstr getStatFormat(struct array *stats, wstr format_stat)
{
    int ret, i = 0;
    char buf[255];
    long long print_val;
    size_t count;
    struct statItem *stat_items;

    count = narray(stats);
    stat_items = arrayData(stats);
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
    } while (++i < count);
    return format_stat;
}

void logStat()
{
    FILE *fp;
    wstr format_stat = wstrEmpty();
    format_stat = getStatFormat(Server.stats, format_stat);
    if (!Server.stat_file) {
        wheatLog(WHEAT_LOG_RAW, "---- Master Statistic Information -----\n");
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
    } else {
        fp = fopen(Server.stat_file, "a");
        if (!fp)
            return;
        fprintf(fp, "%s", format_stat);
        fflush(fp);
        fclose(fp);
    }
    wstrFree(format_stat);
}

void statCommand(struct masterClient *c)
{
    struct listNode *node;
    struct listIterator *iter;
    struct workerProcess *worker;
    wstr format_stat = wstrEmpty();

    if (!wstrCmpNocaseChars(c->argv[1], "master", 6)) {
        format_stat = getStatFormat(Server.stats, format_stat);
    } else if (!wstrCmpNocaseChars(c->argv[1], "worker", 6)) {
        iter = listGetIterator(Server.workers, START_HEAD);
        while ((node = listNext(iter)) != NULL) {
            worker = listNodeValue(node);
            format_stat = getStatFormat(worker->stats, format_stat);
        }
        freeListIterator(iter);
    }
    replyMasterClient(c, format_stat, wstrlen(format_stat));
    wstrFree(format_stat);
}
