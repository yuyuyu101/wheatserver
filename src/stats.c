#include "stats.h"

/* ========== Worker Statistic Area ========== */

static int connectWithMaster()
{
    int fd;

    fd = wheatTcpNonBlockConnect(NULL, Server.bind_addr, Server.port);
    if (fd == NET_WRONG) {
        wheatLog(WHEAT_WARNING,"Unable to connect to MASTER: %s",
            strerror(errno));
        return WHEAT_WRONG;
    }
    if (WorkerProcess->stat->master_stat_fd != 0)
        close(WorkerProcess->stat->master_stat_fd);
    WorkerProcess->stat->master_stat_fd = fd;

    return WHEAT_OK;
}

void initWorkerStat()
{
    WorkerProcess->stat = malloc(sizeof(struct workerStat));
    WorkerProcess->stat->master_stat_fd = 0;
    WorkerProcess->stat->stat_start_time = time(NULL);
    WorkerProcess->stat->stat_total_connection = 0;
    WorkerProcess->stat->stat_total_request = 0;
    WorkerProcess->stat->stat_failed_request = 0;
    WorkerProcess->stat->stat_buffer_size = 0;
    WorkerProcess->stat->stat_work_time = 0;
    connectWithMaster();
}

void sendStatPacket()
{
    struct workerStat *stat = WorkerProcess->stat;
    char buf[WHEAT_STAT_PACKET_MAX];
    stat->stat_last_send = time(NULL);
    snprintf(buf, WHEAT_STAT_PACKET_MAX, WHEAT_STAT_SEND_FORMAT,
            stat->stat_start_time, stat->stat_total_connection,
            stat->stat_total_request, stat->stat_failed_request,
            stat->stat_buffer_size, stat->stat_work_time,
            stat->stat_last_send);
    wstr send = wstrNew(buf);
    size_t nwrite = WorkerProcess->worker->sendData(Server.stat_fd, &send);
    if (nwrite == -1 && errno == EPIPE) {
        wheatLog(WHEAT_DEBUG, "Master close connection");
        connectWithMaster();
    } else if (nwrite != wstrlen(send))
        wheatLog(WHEAT_DEBUG,
                "send statistic info failed, total %d sended %d",
                wstrlen(send), nwrite);
}

/* ========== Master Statistic Area ========== */

static ssize_t handleStat(wstr buf)
{
    ASSERT(wstrlen(buf) > 2);
    if (buf[0] != '\r' || buf[1] != '\r') {
        return WHEAT_WRONG;
    }
    int count = 0;
    pid_t pid;
    ssize_t is_ok = 1;
    struct workerProcess *worker = NULL;
    struct workerStat *stat = NULL;
    wstr *lines = wstrNewSplit(buf+2, "\n", 1, &count);
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
    stat = worker->stat;

    stat->stat_start_time = atoi(lines[1]);
    stat->stat_total_connection = atoi(lines[2]);
    stat->stat_total_request = atoi(lines[3]);
    stat->stat_failed_request = atoi(lines[4]);
    stat->stat_buffer_size = atoi(lines[5]);
    stat->stat_work_time = atoi(lines[6]);
    stat->stat_last_send = atoi(lines[7]);
cleanup:
    wstrFreeSplit(lines, count);
    if (is_ok)
        return WHEAT_OK;
    return WHEAT_WRONG;
}

static void statReadProc(struct evcenter *center, int fd, void *client_data, int mask)
{
    wstr buf;
    size_t nread = readBulkFrom(fd, &buf);
    ssize_t parse_ret = 0;
    if (nread < 10) {
        wheatLog(WHEAT_DEBUG, "receive stat packet less than 10 bits: %s",
                buf);
        return ;
    }
    parse_ret = handleStat(buf);
    if (parse_ret != WHEAT_OK) {
        wheatLog(WHEAT_DEBUG, "parse stat packet failed: %s",
                buf);
    }
}

static void buildConnection(struct evcenter *center, int fd, void *client_data, int mask)
{
    char ip[46];
    int cport, cfd = wheatTcpAccept(Server.neterr, Server.ipfd, ip, &cport);
    if (cfd == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Accepting client connection failed: %s", Server.neterr);
            return;
    }
    wheatLog(WHEAT_VERBOSE, "Accepted %s:%d", ip, cport);

    wheatNonBlock(NULL, cfd);
    wheatTcpNoDelay(NULL, cfd);
    if (createEvent(Server.stat_center, cfd, EVENT_READABLE,
        statReadProc, NULL) == WHEAT_WRONG)
    {
        close(fd);
        return ;
    }
}

void initMasterStats()
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
    Server.stat_center = eventcenter_init(Server.worker_number*2);
    createEvent(Server.stat_center, Server.stat_fd, EVENT_READABLE, buildConnection,  NULL);
}

void statMasterLoop()
{
    processEvents(Server.stat_center);
}

void logStat()
{
    struct listNode *node = NULL;
    struct workerProcess *worker = NULL;
    struct workerStat *stat = worker->stat;
    struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
    wheatLog(WHEAT_LOG_RAW, "---- Now Statistic Information are ----");
    while ((node = listNext(iter)) != NULL) {
        worker = listNodeValue(node);
        wheatLog(WHEAT_LOG_RAW, "Worker: %d", worker->pid);
        wheatLog(WHEAT_LOG_RAW, "Start Time: %d", stat->stat_start_time);
        wheatLog(WHEAT_LOG_RAW, "Total Connection: %d", stat->stat_total_connection);
        wheatLog(WHEAT_LOG_RAW, "Total Request: %d", stat->stat_total_request);
        wheatLog(WHEAT_LOG_RAW, "Failed Request: %d", stat->stat_failed_request);
        wheatLog(WHEAT_LOG_RAW, "Max Buffer Size: %d", stat->stat_buffer_size);
        wheatLog(WHEAT_LOG_RAW, "Refresh Time: %d", stat->stat_last_send);
        wheatLog(WHEAT_LOG_RAW, "-----------------------------------", stat->stat_last_send);
    }
    freeListIterator(iter);
}
