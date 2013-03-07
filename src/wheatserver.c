#include "wheatserver.h"

struct globalServer Server;

void initGlobalServerConfig()
{
    Server.bind_addr = NULL;
    Server.port = WHEAT_SERVERPORT;
    Server.ipfd = 0;
    Server.stat_fd = 0;
    Server.master_center = NULL;
    Server.logfile = NULL;
    Server.verbose = WHEAT_NOTICE;
    Server.worker_number = 2;
    Server.worker_type = NULL;
    Server.graceful_timeout = WHEATSERVER_GRACEFUL_TIME;
    Server.idle_timeout = WHEATSERVER_IDLE_TIME;
    Server.cron_time = time(NULL);
    Server.daemon = 0;
    Server.pidfile = NULL;
    Server.max_buffer_size = WHEAT_MAX_BUFFER_SIZE;
    Server.worker_number = WHEATSERVER_TIMEOUT;
    Server.stat_addr = NULL;
    Server.stat_port = WHEAT_STATS_PORT;
    Server.stat_refresh_seconds = WHEAT_STAT_REFRESH;
    Server.mbuf_size = WHEAT_MBUF_SIZE;
    Server.aggregate_workers_stat = initWorkerStat(1);
    Server.master_stat = initMasterStat();
    initHookCenter();
    Server.workers = createList();
    listSetFree(Server.workers, freeWorkerProcess);
    Server.master_clients = createList();
    Server.pid = getpid();
    Server.relaunch_pid = 0;
    memcpy(Server.master_name, "wheatserver", 12);
    Server.configfile_path[0] = '\0';
    initMasterSignals();
    nonBlockCloseOnExecPipe(&Server.pipe_readfd, &Server.pipe_writefd);
}

static void initMainListen()
{
    Server.ipfd = wheatTcpServer(Server.neterr, Server.bind_addr, Server.port);
    if (Server.ipfd == NET_WRONG || Server.ipfd < 0) {
        wheatLog(WHEAT_WARNING, "Setup tcp server failed port: %d wrong: %s", Server.port, Server.neterr);
        halt(1);
    }
    if (wheatNonBlock(Server.neterr, Server.ipfd) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set nonblock %d failed: %s", Server.ipfd, Server.neterr);
        halt(1);
    }
    wheatLog(WHEAT_NOTICE, "Server is listen port %d", Server.port);
}

void wakeUp()
{
    ssize_t n;
    n = write(Server.pipe_writefd, ".", 1);
    if (n <= 0) {
        if (errno == EAGAIN || errno == EINTR)
            return ;
        wheatLog(WHEAT_WARNING, "wakeUp() can't write 1 byte to pipe: %s", strerror(errno));
        halt(1);
    }
    assert(n == 1);
}

static void handlePipe(struct evcenter *center, int fd, void *client_data, int mask)
{
    ssize_t n;
    char buf[2];
    while ((n = read(Server.pipe_readfd, buf, 1)) <= 0){
        if (errno == EAGAIN || errno == EINTR)
            continue;
        wheatLog(WHEAT_WARNING, "handlePipe() read failed: %s", strerror(errno));
        halt(1);
    }
    ASSERT(buf[0] == '.');
}

/* reap worker avoid zombie */
static void reapWorkers()
{
    int status, result;

    result = waitpid(-1, &status, WNOHANG);
    if (result == 0)
        return ;
    else if (result == -1) {
        if (errno == ECHILD)
            return ;
        else {
            wheatLog(WHEAT_NOTICE, "reapWorkers() waitpid failed: %s", strerror(errno));
        }
    } else {
        if (Server.relaunch_pid == result)
            return ;

        int exitcode = WEXITSTATUS(status);
        int bysignal = 0;

        if (WIFSIGNALED(status))
            bysignal = WTERMSIG(status);
        if (bysignal || exitcode != 0) {
            wheatLog(WHEAT_NOTICE, "reapWorkers() catch worker %d exitcode: %d  signal: %d", result, exitcode, bysignal);
        }
        if (exitcode == WORKER_BOOT_ERROR) {
            wheatLog(WHEAT_WARNING, "Worker failed to boot.");
            halt(1);
        }
        struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
        struct listNode *curr;
        while ((curr = listNext(iter)) != NULL) {
            struct workerProcess *worker = listNodeValue(curr);
            if (worker->pid == result) {
                removeListNode(Server.workers, curr);
                break;
            }
        }
        freeListIterator(iter);
    }
}

void adjustWorkerNumber()
{
    while (listLength(Server.workers) < Server.worker_number)
        spawnWorker(Server.worker_type);
    while (listLength(Server.workers) > Server.worker_number && listLength(Server.workers) > 0) {
        /* worker is append to Server.workers, so the first worker in the `Server.workers` is alive longest */
        struct listNode *max_age_worker = listFirst(Server.workers);
        killWorker(listNodeValue(max_age_worker), SIGQUIT);
        reapWorkers();
    }
}

void spawnWorker(char *worker_name)
{
    pid_t pid;
    struct workerProcess *new_worker = malloc(sizeof(struct workerProcess));
    if (new_worker == NULL) {
        wheatLog(WHEAT_WARNING, "spawn new worker failed: %s", strerror(errno));
        halt(1);
    }

    Server.master_stat->total_run_workers++;
#ifdef WHEAT_DEBUG_WORKER
    pid = 0;
#else
    pid = fork();
#endif
    if (pid != 0) {
        appendToListTail(Server.workers, new_worker);
        new_worker->stat = initWorkerStat(1);
        new_worker->pid = pid;
        new_worker->start_time = Server.cron_time;
        return ;
    } else {
        WorkerProcess = new_worker;
        initWorkerProcess(new_worker, worker_name);
        wheatLog(WHEAT_NOTICE, "new worker %s spawned %d",
                WorkerProcess->worker->name, getpid());
        workerProcessCron();
        wheatLog(WHEAT_NOTICE, "worker %s exit pid:%d",
                WorkerProcess->worker->name, getpid());
        exit(0);
    }
}

void killAllWorkers(int sig)
{
    struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
    struct listNode *current;
    while ((current = listNext(iter)) != NULL) {
        struct workerProcess *w = listNodeValue(current);
        killWorker(w, sig);
    }
    freeListIterator(iter);
}

void killWorker(struct workerProcess *worker, int sig)
{
    int result;
    if ((result = kill(worker->pid, sig)) != 0) {
        wheatLog(WHEAT_NOTICE, "kill %d pid failed: %s", worker->pid, strerror(errno));
    }
}

void reexec()
{
}

void halt(int graceful)
{
    if (WorkerProcess == NULL) {
        stopWorkers(graceful);
        wheatLog(WHEAT_NOTICE, "shutdown %s.", Server.master_name);
    } else {
        wheatLog(WHEAT_NOTICE, "worker %s exit.", WorkerProcess->worker_name);
    }
    if (Server.daemon) unlink(Server.pidfile);
    exit(graceful);
}

void stopWorkers(int graceful)
{
    int sig;
    close(Server.ipfd);

    if (graceful)
        sig = SIGQUIT;
    else
        sig = SIGTERM;
    long seconds = Server.cron_time + Server.graceful_timeout;
    while (seconds < Server.cron_time && listLength(Server.workers)) {
        killAllWorkers(sig);
        usleep(200000);
        reapWorkers();
    }
    killAllWorkers(SIGKILL);
}

static void findTimeoutWorker()
{
    struct listIterator *iter = listGetIterator(Server.workers, START_HEAD);
    struct listNode *node;
    time_t cache_now = Server.cron_time;
    unsigned int timeout = Server.worker_timeout;
    while ((node = listNext(iter)) != NULL) {
        struct workerProcess *worker = listNodeValue(node);
        if (cache_now - worker->stat->refresh_time > timeout) {
            Server.master_stat->timeout_workers++;
            wheatLog(WHEAT_WARNING, "Worker trigger timeout %d, kill it: %d",
                    cache_now - worker->stat->stat_last_send, worker->pid);
            killWorker(worker, SIGTERM);
        }
    }
}

void run()
{
    static long cron_times = 1;
    int *sig;
    adjustWorkerNumber();
    while (1) {
        cron_times++;

        Server.cron_time = time(NULL);
        reapWorkers();
        if (listLength(Server.signal_queue) == 0) {
        // processEvents will refresh worker status, so findTimeoutWorker
        // must follow processEvents in order to avoid incorrect timeout
            processEvents(Server.master_center, WHEATSERVER_CRON);
            adjustWorkerNumber();
            findTimeoutWorker();
            if (!(cron_times % 10) && Server.verbose == WHEAT_DEBUG)
                logStat();
        } else {
            struct listNode *node = listFirst(Server.signal_queue);
            sig = listNodeValue(node);
            signalGenericHandle(*sig);
            removeListNode(Server.signal_queue, node);
            wakeUp();
        }
    }
}

struct masterClient *createMasterClient(int fd)
{
    struct masterClient *c = malloc(sizeof(struct masterClient));
    c->request_buf = wstrEmpty();
    c->response_buf = wstrEmpty();
    c->argc = 0;
    c->argv = NULL;
    c->fd = fd;
    appendToListTail(Server.master_clients, c);
    return c;
}

void freeMasterClient(struct masterClient *c)
{
    close(c->fd);
    wstrFree(c->request_buf);
    wstrFree(c->response_buf);
    if (c->argv)
        wstrFreeSplit(c->argv, c->argc);
    deleteEvent(Server.master_center, c->fd, EVENT_READABLE);
    deleteEvent(Server.master_center, c->fd, EVENT_WRITABLE);
    struct listNode *node = searchListKey(Server.master_clients, c);
    ASSERT(node);
    removeListNode(Server.master_clients, node);
    wheatLog(WHEAT_DEBUG, "delete readable fd: %d", c->fd);
    free(c);
}

static void resetMasterClient(struct masterClient *c)
{
    if (c->argc != 0)
        wstrFreeSplit(c->argv, c->argc);
    c->argc = 0;
    c->argv = NULL;
}

static ssize_t processCommand(struct masterClient *c)
{
    if (c->argc == WHEAT_STATCOMMAND_PACKET_FIELD
            && !wstrCmpNocaseChars(c->argv[0], "statinput", 9)) {
        statinputCommand(c);
    } else if (c->argc == 2 && !wstrCmpNocaseChars(c->argv[0], "config", 6)) {
        configCommand(c);
    } else if(c->argc == 2 && !wstrCmpNocaseChars(c->argv[0], "stat", 4)) {
        statCommand(c);
    }

    return WHEAT_OK;
}

static void commandParse(struct evcenter *center, int fd, void *client_data, int mask)
{
    struct masterClient *client = client_data;
    struct slice slice;
    client->request_buf = wstrMakeRoom(client->request_buf, 512);
    sliceTo(&slice, (uint8_t *)client->request_buf+wstrlen(client->request_buf),
            wstrfree(client->request_buf));
    ssize_t nread = readBulkFrom(fd, &slice);
    if (nread == -1) {
        freeMasterClient(client);
        return ;
    }
    wstrupdatelen(client->request_buf, nread);
    while (wstrlen(client->request_buf)) {
        int start, end;
        int count = 0;
        wstr packet;
        start = wstrIndex(client->request_buf, '\r');
        if (start == -1 || start >= wstrlen(client->request_buf) - 4 ||
                client->request_buf[start+1] != '\r')
            break;
        end = wstrIndex(client->request_buf, '$');
        if (end == -1)
            break;
        start += 2;
        packet = wstrNewLen(client->request_buf+start, end-start);
        if (end+1 == wstrlen(packet)) {
            wstrRange(client->request_buf, 0, 0);
        } else {
            wstrRange(client->request_buf, end+1, 0);
        }
        client->argv = wstrNewSplit(packet, "\n", 1, &count);
        if (!client->argv || count < 1)
            break;
        client->argc = count;
        processCommand(client);
        resetMasterClient(client);
        wstrFree(packet);
    }
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

    struct masterClient *c = createMasterClient(cfd);

    createEvent(center, cfd, EVENT_READABLE, commandParse, c);
}

void initStatListen()
{
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

    if (createEvent(Server.master_center, Server.stat_fd, EVENT_READABLE, buildConnection,  NULL) == WHEAT_WRONG)
    {
        wheatLog(WHEAT_WARNING, "createEvent failed");
        halt(1);
    }
}

/* reload do some works below:
 * 1. reload config file
 * 2. spawn new workers
 * 3. kill old wokers
 * 4. reboot statistic server */
void reload()
{
    char *old_addr = Server.bind_addr, *old_stat_addr = Server.stat_addr;
    int old_port = Server.port, old_stat_port = Server.stat_port;
    loadConfigFile(Server.configfile_path, NULL);
    if (strlen(Server.bind_addr) != strlen(old_addr) ||
            strncmp(Server.bind_addr, old_addr, strlen(old_addr)) ||
            old_port != Server.port) {
        close(Server.ipfd);
        initMainListen();
    }

    if (strlen(Server.stat_addr) != strlen(old_stat_addr) ||
            strncmp(Server.stat_addr, old_stat_addr, strlen(old_stat_addr)) ||
            old_stat_port != Server.stat_port) {
        close(Server.stat_fd);
        deleteEvent(Server.master_center, Server.stat_port, EVENT_READABLE);
        initStatListen();
    }
    logRedirect();

    int i;
    for (i = 0; i < Server.worker_number; i++) {
        spawnWorker(Server.worker_type);
    }
    if (Server.daemon) createPidFile();

    adjustWorkerNumber();
}

void initServer()
{
    Server.master_center = eventcenter_init(Server.worker_number*2+32);
    if (!Server.master_center) {
        wheatLog(WHEAT_WARNING, "eventcenter_init failed");
        halt(1);
    }

   if (createEvent(Server.master_center, Server.pipe_readfd, EVENT_READABLE, handlePipe,  NULL) == WHEAT_WRONG)
    {
        wheatLog(WHEAT_WARNING, "createEvent failed");
        halt(1);
    }

    initStatListen();
    initMainListen();
    logRedirect();
}

void version() {
    fprintf(stderr, "wheatserver %s\n", WHEATSERVER_VERSION);
    exit(0);
}

void usage() {
    fprintf(stderr,"Usage: ./wheatserver [/path/to/wheatserver.conf] [options]\n");
    fprintf(stderr,"       ./wheatserver - (read config from stdin)\n");
    fprintf(stderr,"       ./wheatserver -v or --version\n");
    fprintf(stderr,"       ./wheatserver -h or --help\n");
    fprintf(stderr,"Examples:\n");
    fprintf(stderr,"       ./wheatserver (run the server with default conf)\n");
    fprintf(stderr,"       ./wheatserver /etc/redis/10828.conf\n");
    fprintf(stderr,"       ./wheatserver --port 10828\n");
    fprintf(stderr,"       ./wheatserver /etc/wheatserver.conf --loglevel verbose\n\n");
    exit(1);
}

int main(int argc, const char *argv[])
{
    initGlobalServerConfig();

    wstr options = wstrEmpty();
    if (argc >= 2) {
        int j = 1;

        if (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0) version();
        if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0) usage();

        if (argv[j][0] != '-' && argv[j][1] != '-')
            strncpy(Server.configfile_path, argv[j++], WHEATSERVER_MAX_NAMELEN);
        while(j != argc) {
            if (argv[j][0] == '-' && argv[j][1] == '-') {
                if (wstrlen(options))
                    options = wstrCat(options,"\n");
                options = wstrCat(options,argv[j]+2);
                options = wstrCat(options," ");
            } else {
                options = wstrCat(options, argv[j]);
                options = wstrCat(options," ");
            }
            j++;
        }
    } else {
        wheatLog(WHEAT_NOTICE, "No config file specified, use the default settings");
    }
    loadConfigFile(Server.configfile_path, options);
    wstrFree(options);
    if (Server.daemon) daemonize(1);
    wheatLog(WHEAT_NOTICE, "WheatServer v%s is running", WHEATSERVER_VERSION);

    initServer();
    if (Server.daemon) createPidFile();
    setProctitle(argv[0]);

    run();
    return 0;
}
