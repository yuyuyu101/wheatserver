#include "stats.h"

static void handleStat(struct workerStat *left, struct workerStat *add)
{
    left->stat_total_connection += add->stat_total_connection;
    left->stat_total_request += add->stat_total_request;
    left->stat_failed_request += add->stat_failed_request;
    if (left->stat_buffer_size < add->stat_buffer_size)
        left->stat_buffer_size = add->stat_buffer_size;
    left->stat_work_time += add->stat_work_time;
    left->stat_last_send = add->stat_last_send;
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
            stat->stat_failed_request, stat->stat_buffer_size,
            stat->stat_work_time, stat->stat_last_send);
    wstr send = wstrNew(buf);
    ssize_t nwrite = WorkerProcess->worker->sendData(stat->master_stat_fd, &send);
    if (nwrite == -1) {
        wheatLog(WHEAT_DEBUG, "Master close connection");
        connectWithMaster(WorkerProcess->stat);
    } else if (nwrite != wstrlen(send))
        wheatLog(WHEAT_DEBUG,
                "send statistic info failed, total %d sended %d",
                wstrlen(send), nwrite);
    else {
        resetStat(stat);
        stat->stat_last_send = time(NULL);
    }
    wstrFree(send);
}

/* ========== Master Statistic Area ========== */

static ssize_t parseStat(wstr buf)
{
    ASSERT(wstrlen(buf) > 2);
    if (buf[0] != '\r' || buf[1] != '\r') {
        return WHEAT_WRONG;
    }
    int end = wstrIndex(buf, '.'), len;
    if (end == -1)
        return WHEAT_WRONG;
    len = end + 1;

    wstr packet = wstrNewLen(buf, len);
    buf = wstrRange(buf, len, 0);
    ssize_t is_ok = 1;
    int count;
    pid_t pid;
    struct workerProcess *worker = NULL;
    struct workerStat *stat = initStat(1);
    wstr *lines = wstrNewSplit(packet, "\n", 1, &count);
    // extra one for worker pid
    if (count != WHEAT_STAT_FIELD + 1) {
        is_ok = 0;
        goto cleanup;
    }
    pid = atoi(lines[0]);
    struct listNode *node = NULL;
    struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        worker = listNodeValue(node);
        if (worker->pid == pid)
            break;
    }
    freeListIterator(iter);

    if (!worker) {
        is_ok = 0;
        goto cleanup;
    }

    stat->stat_total_connection = atoi(lines[1]);
    stat->stat_total_request = atoi(lines[2]);
    stat->stat_failed_request = atoi(lines[3]);
    stat->stat_buffer_size = atoi(lines[4]);
    stat->stat_work_time = atoi(lines[5]);
    stat->stat_last_send = atoi(lines[6]);

    handleStat(worker->stat, stat);
    handleStat(Server.stat, stat);

cleanup:
    wstrFreeSplit(lines, count);
    wstrFree(packet);
    free(stat);
    if (is_ok)
        return WHEAT_OK;
    return WHEAT_WRONG;
}

static void statReadProc(struct evcenter *center, int fd, void *client_data, int mask)
{
    wstr buf = wstrEmpty();
    ssize_t nread = readBulkFrom(fd, &buf);
    ssize_t parse_ret = WHEAT_OK;
    if (nread == -1) {
        close(fd);
        deleteEvent(center, fd, EVENT_READABLE);
        wheatLog(WHEAT_DEBUG, "delete readable fd: %d", fd);
        return ;
    } else if (nread < 10) {
        wheatLog(WHEAT_DEBUG, "receive stat packet less than 10 bits: %s",
                buf);
        return ;
    }
    while (wstrlen(buf)) {
        parse_ret = parseStat(buf);
        if (parse_ret == WHEAT_OK) {
            wheatLog(WHEAT_DEBUG, "receive worker statistic info %d", fd);
        }
        else {
            wheatLog(WHEAT_DEBUG, "parse stat packet failed: %s",
                    buf);
            break;
        }
    }
    wstrFree(buf);
}

static void buildConnection(struct evcenter *center, int fd, void *client_data, int mask)
{
    char ip[46];
    int cport, cfd = wheatTcpAccept(Server.neterr, fd, ip, &cport);
    if (cfd == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Accepting client connection failed: %s", Server.neterr);
            return;
    }
    wheatLog(WHEAT_DEBUG, "Accepted worker %s:%d", ip, cport);

    if (wheatNonBlock(Server.neterr, cfd) == NET_WRONG) {
            wheatLog(WHEAT_WARNING,
                    "buildConnection: set nonblock %d failed: %s",
                    fd, Server.neterr);
            return ;
    }
    if (wheatTcpNoDelay(Server.neterr, cfd) == NET_WRONG) {
        wheatLog(WHEAT_WARNING,
                "buildConnection: tcp no delay %d failed: %s",
                fd, Server.neterr);
        return ;
    }
    if (createEvent(Server.stat_center, cfd, EVENT_READABLE,
        statReadProc, NULL) == WHEAT_WRONG)
    {
        close(fd);
        return ;
    }
}

void resetStat(struct workerStat *stat)
{
    int fd = stat->master_stat_fd;
    memset(stat, 0, sizeof(struct workerStat));
    stat->master_stat_fd = fd;
}

void initMasterStatsServer()
{
    if (Server.stat_port != 0) {
        Server.stat_fd = wheatTcpServer(Server.neterr,
                Server.stat_addr, Server.stat_port);
        if (Server.stat_fd == NET_WRONG || Server.stat_fd < 0) {
            wheatLog( WHEAT_WARNING,
                    "Setup tcp server failed port: %d wrong: %s",
                    Server.stat_port, Server.neterr);
            halt(1);
        }
        if (wheatNonBlock(Server.neterr, Server.stat_fd) == NET_WRONG) {
            wheatLog(WHEAT_WARNING,
                    "Set nonblock %d failed: %s",
                    Server.stat_fd, Server.neterr);
            halt(1);
        }
        if (wheatCloseOnExec(Server.neterr, Server.stat_fd) == NET_WRONG) {
            wheatLog(WHEAT_WARNING,
                    "Set close on exec %d failed: %s",
                    Server.stat_fd, Server.neterr);
        }

        wheatLog(WHEAT_NOTICE, "Stat Server is listen port %d",
                Server.stat_port);
    }
    Server.stat_center = eventcenter_init(Server.worker_number*2+32);
    if (createEvent(Server.stat_center, Server.stat_fd, EVENT_READABLE, buildConnection,  NULL) == WHEAT_WRONG)
    {
        wheatLog(WHEAT_WARNING, "createEvent failed");
        return ;
    }

}

static time_t now = 0;
void statMasterLoop()
{
    if (now == 0)
        now = time(NULL);
    processEvents(Server.stat_center, Server.stat_refresh_seconds);
    if (time(NULL) - now > 5) {
        if (Server.verbose == WHEAT_DEBUG)
            logStat();
        now = time(NULL);
    }
}

static void logWorkerStatFormt(struct workerStat *stat)
{
    wheatLog(WHEAT_LOG_RAW, "Total Connection: %lld\n", stat->stat_total_connection);
    wheatLog(WHEAT_LOG_RAW, "Total Request: %lld\n", stat->stat_total_request);
    wheatLog(WHEAT_LOG_RAW, "Failed Request: %lld\n", stat->stat_failed_request);
    wheatLog(WHEAT_LOG_RAW, "Max Buffer Size: %lld\n", stat->stat_buffer_size);
    wheatLog(WHEAT_LOG_RAW, "Work Time: %llds\n", stat->stat_work_time/100000);
    wheatLog(WHEAT_LOG_RAW, "Refresh Time: %s\n", ctime(&stat->stat_last_send));
}

void logStat()
{
    struct listNode *node = NULL;
    struct workerProcess *worker = NULL;
    struct workerStat *stat;
    wheatLog(WHEAT_LOG_RAW, "---- Master Statistic Information -----\n");
    logWorkerStatFormt(Server.stat);
    wheatLog(WHEAT_LOG_RAW, "-- Workers Statistic Information are --\n");
    struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        worker = listNodeValue(node);
        stat = worker->stat;
        wheatLog(WHEAT_LOG_RAW, "\nStart Time: %s", ctime(&worker->start_time));
        logWorkerStatFormt(stat);
    }
    freeListIterator(iter);
    wheatLog(WHEAT_LOG_RAW, "---------------------------------------\n");
}

struct workerStat *initStat(int only_malloc)
{
    struct workerStat *stat = malloc(sizeof(struct workerStat));
    stat->master_stat_fd = 0;
    stat->stat_total_connection = 0;
    stat->stat_total_request = 0;
    stat->stat_failed_request = 0;
    stat->stat_buffer_size = 0;
    stat->stat_work_time = 0;
    if (!only_malloc)
        connectWithMaster(stat);
    return stat;
}
