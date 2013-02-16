#include "wheatserver.h"

#define MASTER_SIGNAL_COUNT 10
#define WORKER_SIGNAL_COUNT 11
static int signals[11] = {SIGHUP, SIGQUIT, SIGINT, SIGTERM,
    SIGTTIN, SIGTTOU, SIGUSR1, SIGUSR2, SIGWINCH, SIGCHLD,
    SIGSEGV
};

/* int list */
void *dupInt(void *ptr)
{
    int *new_ptr = malloc(sizeof(int));
    *new_ptr = *(int *)ptr;
    return new_ptr;
}

void freeInt(void *ptr)
{
    free(ptr);
}

int matchInt(void *ptr, void *key)
{
    return (*(int *)ptr == *(int *)key);
}

static struct list *createIntList()
{
    struct list *l = createList();
    listSetDup(l, dupInt);
    listSetFree(l, freeInt);
    listSetMatch(l, matchInt);
    return l;
}

/* Signal Process */

void initWorkerSignals()
{
    struct sigaction act, oact;
    int i;

    for (i = 0; i < WORKER_SIGNAL_COUNT; i++) {
        act.sa_flags = 0;
        if (signals[i] == SIGQUIT)
            act.sa_handler = handleWorkerQuit;
        else if (signals[i] == SIGTERM || signals[i] == SIGINT)
            act.sa_handler = handleWorkerAbort;
        else if (signals[i] == SIGUSR1)
            act.sa_handler = handleWorkerUsr1;
        else if (signals[i] == SIGSEGV) {
            act.sa_flags = (int)SA_RESETHAND;
            act.sa_handler = handleSegv;
        }
        else
            act.sa_handler = SIG_DFL;
        sigemptyset(&act.sa_mask);
        if (sigaction(signals[i], &act, &oact) != 0) {
            wheatLog(WHEAT_NOTICE, "sigaction error on %d : %s", signals[i], strerror(errno));
        }
    }
}

void initMasterSignals()
{
    Server.signal_queue = createIntList();
    struct sigaction act, oact;

    act.sa_handler = signalProc;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    int i;
    for (i = 0; i < MASTER_SIGNAL_COUNT; i++) {
        if (sigaction(signals[i], &act, &oact) != 0) {
            wheatLog(WHEAT_WARNING, "sigaction error on %d : %s", signals[i], strerror(errno));
        }
    }

    act.sa_handler = handleSegv;
    sigemptyset(&act.sa_mask);
    act.sa_flags = (int)SA_RESETHAND;
    if (sigaction(signals[10], &act, &oact) != 0) {
            wheatLog(WHEAT_WARNING, "sigaction error on %d : %s", signals[i], strerror(errno));
    }
}

void signalGenericHandle(int sig)
{
    if (sig == SIGCHLD)
        handleChld();
    else if (sig == SIGHUP)
        handleHup();
    else if (sig == SIGQUIT)
        handleQuit();
    else if (sig == SIGINT)
        handleInt();
    else if (sig == SIGTERM)
        handleTerm();
    else if (sig == SIGTTIN)
        handleTtin();
    else if (sig == SIGTTOU)
        handleTtou();
    else if (sig == SIGUSR1)
        handleUsr1();
    else if (sig == SIGUSR2)
        handleUsr2();
    else if (sig == SIGWINCH)
        handleWinch();
}

void signalProc(int signal)
{
    if (listLength(Server.signal_queue) < 5) {
        if (signal != SIGCHLD)
            appendToListTail(Server.signal_queue, &signal);
        wakeUp();
    }
}

/* ========== Master Singal Handler ========== */

void handleChld()
{
    wakeUp();
}

/* HUP handling:
*  Reload configuration
*  Start the new worker processes with a new configuration
*  Gracefully shutdown the old worker processes */
void handleHup()
{
    wheatLog(WHEAT_NOTICE, "Hang up: %s", Server.master_name);
    reload();
}

void handleQuit()
{
    wheatLog(WHEAT_WARNING, "Signal sigquit");
    halt(1);
}

void handleInt()
{
    wheatLog(WHEAT_WARNING, "Signal int");
    halt(0);
}

void handleTerm()
{
    wheatLog(WHEAT_WARNING, "Signal term");
    halt(0);
}

void handleTtin()
{
    Server.worker_number++;
    adjustWorkerNumber();
}

void handleTtou()
{
    Server.worker_number--;
    adjustWorkerNumber();
}

void handleUsr1()
{
    wheatLog(WHEAT_NOTICE, "Signal usr1: %s", Server.master_name);
    killAllWorkers(SIGUSR1);
    logStat();
}

/* SIGUSR2 handling.  Creates a new master/worker set as a
 * slave of the current * master without affecting old workers.
 * Use this to do live deployment with the ability to backout a change. */
void handleUsr2()
{
    wheatLog(WHEAT_NOTICE, "Signal usr2: %s reexec", Server.master_name);
    reexec();
}

void handleWinch()
{
    if (getppid() == 1 || getpgrp() != getpid()) {
        wheatLog(WHEAT_NOTICE, "Signal winch: graceful stop of workers");
        Server.worker_number = 0;
        killAllWorkers(SIGQUIT);
    } else {
        wheatLog(WHEAT_NOTICE, "Signal winch ignored, not daemonize");
    }
}

/* ========== Worker Singal Handler ========== */
void handleWorkerUsr1(int sig)
{
    sendStatPacket();
    return ;
}

void handleWorkerAbort(int sig)
{
    WorkerProcess->alive = 0;
    halt(1);
}

void handleWorkerQuit(int sig)
{
    WorkerProcess->alive = 0;
}

void handleSegv()
{
    wheat_stacktrace(1);
    wheatLogRaw(WHEAT_NOTICE, "Signal SEGV received, core dump");
    raise(SIGSEGV);
}
