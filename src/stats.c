 #include "wheatserver.h"

static void handleStat(struct workerStat *left, struct workerStat *add)
{
    left->stat_total_connection += add->stat_total_connection;
    left->stat_total_request += add->stat_total_request;
    left->stat_timeout_request += add->stat_timeout_request;
    left->stat_failed_request += add->stat_failed_request;
    if (left->stat_buffer_size < add->stat_buffer_size)
        left->stat_buffer_size = add->stat_buffer_size;
    left->stat_work_time += add->stat_work_time;
    left->stat_last_send = add->stat_last_send;

    left->refresh_time = Server.cron_time;
}

/* ========== Worker Statistic Area ========== */

static int connectWithMaster(struct workerStat *stat)
{
    int fd;

    fd = wheatTcpConnect(Server.neterr, Server.stat_addr, Server.stat_port);
    if (fd == NET_WRONG) {
        wheatLog(WHEAT_WARNING,"Unable to connect to MASTER: %s %s",
            strerror(errno), Server.neterr);
        return WHEAT_WRONG;
    }
    if (stat->master_stat_fd != 0)
        close(stat->master_stat_fd);
    stat->master_stat_fd = fd;

    return WHEAT_OK;
}

void sendStatPacket()
{
    struct workerStat *stat = WorkerProcess->stat;
    char buf[WHEAT_STAT_PACKET_MAX];
    snprintf(buf, WHEAT_STAT_PACKET_MAX, WHEAT_STAT_SEND_FORMAT,
            WorkerProcess->pid,
            stat->stat_total_connection, stat->stat_total_request,
            stat->stat_timeout_request, stat->stat_failed_request,
            stat->stat_buffer_size, stat->stat_work_time,
            stat->stat_last_send);
    wstr send = wstrNew(buf);
    ssize_t nwrite = writeBulkTo(stat->master_stat_fd, &send);
    if (nwrite == -1) {
        wheatLog(WHEAT_DEBUG, "Master close connection");
        connectWithMaster(WorkerProcess->stat);
    } else if (wstrlen(send) != 0) {
        wheatLog(WHEAT_DEBUG,
                "send statistic info failed, total %d sended %d",
                wstrlen(send), nwrite);
    } else {
        resetStat(stat);
        stat->stat_last_send = time(NULL);
    }
    wstrFree(send);
}

struct workerStat *initWorkerStat(int only_malloc)
{
    struct workerStat *stat = malloc(sizeof(struct workerStat));
    stat->master_stat_fd = 0;
    stat->stat_total_connection = 0;
    stat->stat_total_request = 0;
    stat->stat_failed_request = 0;
    stat->stat_timeout_request = 0;
    stat->stat_buffer_size = 0;
    stat->stat_work_time = 0;
    stat->stat_last_send = time(NULL);
    stat->refresh_time = time(NULL);
    if (!only_malloc)
        connectWithMaster(stat);
    return stat;
}

/* ========== Master Statistic Area ========== */

static ssize_t parseStat(struct masterClient *client, struct workerStat *stat, struct workerProcess **owner)
{
    ssize_t is_ok;
    pid_t pid;
    if (client->argc != WHEAT_STATCOMMAND_PACKET_FIELD)
        return WHEAT_WRONG;
    pid = atoi(client->argv[1]);
    struct listNode *node = NULL;
    struct workerProcess *worker = NULL;
    struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        worker = listNodeValue(node);
        if (worker->pid == pid)
            break;
    }
    freeListIterator(iter);

    if (!worker) {
        is_ok = 0;
        return WHEAT_WRONG;
    }
    *owner = worker;

    stat->stat_total_connection = atoi(client->argv[2]);
    stat->stat_total_request = atoi(client->argv[3]);
    stat->stat_timeout_request = atoi(client->argv[4]);
    stat->stat_failed_request = atoi(client->argv[5]);
    stat->stat_buffer_size = atoi(client->argv[6]);
    stat->stat_work_time = atoi(client->argv[7]);
    stat->stat_last_send = atoi(client->argv[8]);

    return WHEAT_OK;
}

void statinputCommand(struct masterClient *client)
{
    wstr buf = client->request_buf;
    struct workerProcess *worker;
    struct workerStat stat;
    ssize_t parse_ret;
    parse_ret = parseStat(client, &stat, &worker);
    if (parse_ret == WHEAT_OK) {
        wheatLog(WHEAT_DEBUG, "receive worker statistic info %d", client->fd);
        handleStat(worker->stat, &stat);
        handleStat(Server.aggregate_workers_stat, &stat);
    }
    else {
        wheatLog(WHEAT_DEBUG, "parse stat packet failed: %s",
                buf);
    }
}

void resetStat(struct workerStat *stat)
{
    int fd = stat->master_stat_fd;
    memset(stat, 0, sizeof(struct workerStat));
    stat->master_stat_fd = fd;
}

struct masterStat *initMasterStat()
{
    struct masterStat *stat = malloc(sizeof(struct masterStat));
    stat->total_run_workers = 0;
    stat->timeout_workers = 0;
    return stat;
}

static size_t getMasterStatFormat(struct masterStat *stat, char *buf, size_t len)
{
    size_t pos = 0;
    ssize_t ret = 0;
    ret = snprintf(buf+pos, len-pos, "Total workers spawned: %d\n", stat->total_run_workers);
    if (ret > 0) pos += ret;
    ret = snprintf(buf+pos, len-pos, "Total timeout workers killed: %d\n", stat->timeout_workers);
    return pos;
}

static size_t getWorkerStatFormat(struct workerStat *stat, char *buf, size_t len)
{
    size_t pos = 0;
    ssize_t ret = 0;
    ret = snprintf(buf+pos, len-pos, "Total Connection: %lld\n", stat->stat_total_connection);
    if (ret > 0) pos += ret;
    ret = snprintf(buf+pos, len-pos, "Total Request: %lld\n", stat->stat_total_request);
    if (ret > 0) pos += ret;
    ret = snprintf(buf+pos, len-pos, "Timeout Request: %lld\n", stat->stat_timeout_request);
    if (ret > 0) pos += ret;
    ret = snprintf(buf+pos, len-pos, "Failed Request: %lld\n", stat->stat_failed_request);
    if (ret > 0) pos += ret;
    ret = snprintf(buf+pos, len-pos, "Max Buffer Size: %lld\n", stat->stat_buffer_size);
    if (ret > 0) pos += ret;
    ret = snprintf(buf+pos, len-pos, "Work Time: %llds\n", stat->stat_work_time/100000);
    if (ret > 0) pos += ret;
    ret = snprintf(buf+pos, len-pos, "Last Send Time: %s", ctime(&stat->stat_last_send));
    if (ret > 0) pos += ret;
    ret = snprintf(buf+pos, len-pos, "Refresh Time: %s", ctime(&stat->refresh_time));
    return pos;
}

void logStat()
{
    struct listNode *node = NULL;
    struct workerProcess *worker = NULL;
    struct workerStat *stat;
    char buf[1024];
    wheatLog(WHEAT_LOG_RAW, "---- Master Statistic Information -----\n");
    getMasterStatFormat(Server.master_stat, buf, 1024);
    wheatLog(WHEAT_LOG_RAW, "%s", buf);
    getWorkerStatFormat(Server.aggregate_workers_stat, buf, 1024);
    wheatLog(WHEAT_LOG_RAW, "%s", buf);
    wheatLog(WHEAT_LOG_RAW, "-- Workers Statistic Information are --\n");
    struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        worker = listNodeValue(node);
        stat = worker->stat;
        wheatLog(WHEAT_LOG_RAW, "\nStart Time: %s", ctime(&worker->start_time));
        getWorkerStatFormat(stat, buf, 1024);
        wheatLog(WHEAT_LOG_RAW, "%s", buf);
    }
    freeListIterator(iter);
    wheatLog(WHEAT_LOG_RAW, "---------------------------------------\n");
}

void statCommand(struct masterClient *c)
{
    char buf[1024];
    ssize_t len = 0;
    if (!wstrCmpNocaseChars(c->argv[1], "master", 6)) {
        len = getMasterStatFormat(Server.master_stat, buf, 1024);
    } else if (!wstrCmpNocaseChars(c->argv[1], "worker", 6)) {
        len = getWorkerStatFormat(Server.aggregate_workers_stat, buf, 1024);
    }
    replyMasterClient(c, buf, len);
}

