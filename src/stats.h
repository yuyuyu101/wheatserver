#ifndef _STATS_H_
#define _STATS_H_

#include "wheatserver.h"

#define WHEAT_STAT_FIELD       7
#define WHEAT_STAT_SEND_FORMAT \
    "\r\r%d\n%ld\n%lld\n%lld\n%lld\n%lld\n%lld\n%ld"

// If want to add or delete statistic field, pay attention to place
// 1. where handle this field
// 2. modify statements send and parse statistic packet
// 3. logWorkerStatFormt()
// 4. aggregateStats()
struct workerStat {
    int master_stat_fd;

    // Worker Statistic
    time_t stat_start_time;             // Worker process start time
    long long stat_total_connection;    // Number of the total client
    long long stat_total_request;       // Number of the total request parsed
    // Number of the failed request parsed. Such as http protocol, non-200
    // response is included
    long long stat_failed_request;
    // Max size of request buffer size since worker started
    long long stat_buffer_size;
    long long stat_work_time;           // Total of handling request time
    time_t stat_last_send;           // Time since last send.
};

struct workerStat *initStat(int only_malloc);
void resetStat(struct workerStat *);
void sendStatPacket();
void statMasterLoop();
void logStat();

#endif
