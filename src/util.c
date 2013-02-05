#include "wheatserver.h"
#include <execinfo.h>
#include <sys/stat.h>
#include <fcntl.h>

void nonBlockCloseOnExecPipe(int *fd0, int *fd1)
{
    int full_pipe[2];
    if (pipe(full_pipe) != 0) {
        wheatLog(WHEAT_WARNING, "Create pipe failed: %s", strerror(errno));
        halt(1);
    }
    if (wheatNonBlock(Server.neterr, full_pipe[0]) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set pipe %d nonblock failed: %s", full_pipe[0], Server.neterr);
        halt(1);
    }
    if (wheatNonBlock(Server.neterr, full_pipe[1]) == NET_WRONG) {
        wheatLog(WHEAT_WARNING, "Set pipe %d nonblock failed: %s", full_pipe[1], Server.neterr);
        halt(1);
    }
    *fd0 = full_pipe[0];
    *fd1 = full_pipe[1];
}


unsigned int dictWstrHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, wstrlen((char*)key));
}

unsigned int dictWstrCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, wstrlen((char*)key));
}

int dictWstrKeyCompare(const void *key1,
        const void *key2)
{
    int l1,l2;

    l1 = wstrlen((wstr)key1);
    l2 = wstrlen((wstr)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void dictWstrDestructor(void *val)
{
    wstrFree(val);
}

struct dictType wstrDictType = {
    dictWstrHash,               /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictWstrKeyCompare,         /* key compare */
    dictWstrDestructor,         /* key destructor */
    dictWstrDestructor,         /* val destructor */
};


/* ========== Configuration Validator/Print Area ========== */

int stringValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);

    if (conf->target.ptr) {
        free(conf->target.ptr);
    }
    conf->target.ptr = strdup(val);
    return VALIDATE_OK;
}

int unsignedIntValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);
    int i, digit;
    intptr_t max_limit = 0;

    if (conf->helper)
        max_limit = (intptr_t)conf->helper;

    for (i = 0; val[i] != '\0'; i++) {
        digit = val[i] - 48;
        if (digit < 0 || digit > 9)
            return VALIDATE_WRONG;
        if (max_limit != 0 && digit > max_limit)
            return VALIDATE_WRONG;
    }
    conf->target.val = atoi(val);
    return VALIDATE_OK;
}

int enumValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);
    ASSERT(conf->helper);

    struct enumIdName *sentinel = (struct enumIdName *)conf->helper;
    while (sentinel->name) {
        if (strncasecmp(val, sentinel->name, strlen(sentinel->name)) == 0) {
            conf->target.enum_ptr = sentinel;
            return VALIDATE_OK;
        }
        else if (sentinel == NULL)
            break ;
        sentinel++;
    }

    return VALIDATE_WRONG;
}

int boolValidator(struct configuration *conf, const char *key, const char *val)
{
    size_t len = strlen(val) + 1;
    if (memcmp("on", val, len) == 0) {
        conf->target.val = 1;
    } else if (memcmp("off", val, len) == 0) {
        conf->target.val = 0;
    } else
        return VALIDATE_WRONG;
    return VALIDATE_OK;
}

/* ========== Trace Area ========== */

void wheat_stacktrace(int skip_count)
{
    void *stack[64];
    char **symbols;
    int size, i, j;

    size = backtrace(stack, 64);
    symbols = backtrace_symbols(stack, size);
    if (symbols == NULL) {
        return;
    }

    skip_count++; /* skip the current frame also */

    for (i = skip_count, j = 0; i < size; i++, j++) {
        wheatLog(WHEAT_WARNING, "[%d] %s", j, symbols[i]);
    }

    free(symbols);
}

void wheat_assert(const char *cond, const char *file, int line, int panic)
{
    wheatLog(WHEAT_WARNING, "assert '%s' failed @ (%s, %d)", cond, file, line);
    if (panic) {
        wheat_stacktrace(1);
        abort();
    }
}


int daemonize(int dump_core)
{
    int status;
    pid_t pid, sid;
    int fd;

    pid = fork();
    switch (pid) {
    case -1:
        wheatLog(WHEAT_WARNING, "fork() failed: %s", strerror(errno));
        return WHEAT_WRONG;

    case 0:
        break;

    default:
        /* parent terminates */
        _exit(0);
    }

    /* 1st child continues and becomes the session leader */

    sid = setsid();
    if (sid < 0) {
        wheatLog(WHEAT_WARNING, "setsid() failed: %s", strerror(errno));
        return WHEAT_WRONG;
    }

    pid = fork();
    switch (pid) {
    case -1:
        wheatLog(WHEAT_WARNING, "fork() failed: %s", strerror(errno));
        return WHEAT_WRONG;

    case 0:
        break;

    default:
        /* 1st child terminates */
        _exit(0);
    }

    /* 2nd child continues */

    /* change working directory */
    if (dump_core == 0) {
        status = chdir("/");
        if (status < 0) {
            wheatLog(WHEAT_WARNING, "chdir(\"/\") failed: %s", strerror(errno));
            return WHEAT_WRONG;
        }
    }

    /* clear file mode creation mask */
    umask(0);

    /* redirect stdin, stdout and stderr to "/dev/null" */

    fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        wheatLog(WHEAT_WARNING, "open(\"/dev/null\") failed: %s", strerror(errno));
        return WHEAT_WRONG;
    }

    status = dup2(fd, STDIN_FILENO);
    if (status < 0) {
        wheatLog(WHEAT_WARNING, "dup2(%d, STDIN) failed: %s", fd, strerror(errno));
        close(fd);
        return WHEAT_WRONG;
    }

    status = dup2(fd, STDOUT_FILENO);
    if (status < 0) {
        wheatLog(WHEAT_WARNING, "dup2(%d, STDOUT) failed: %s", fd, strerror(errno));
        close(fd);
        return WHEAT_WRONG;
    }

    status = dup2(fd, STDERR_FILENO);
    if (status < 0) {
        wheatLog(WHEAT_WARNING, "dup2(%d, STDERR) failed: %s", fd, strerror(errno));
        close(fd);
        return WHEAT_WRONG;
    }

    if (fd > STDERR_FILENO) {
        status = close(fd);
        if (status < 0) {
            wheatLog(WHEAT_WARNING, "close(%d) failed: %s", fd, strerror(errno));
            return WHEAT_WRONG;
        }
    }

    return WHEAT_OK;
}

void createPidFile() {
    /* Try to write the pid file in a best-effort way. */
    FILE *fp = fopen(Server.pidfile, "w");
    if (fp) {
        fprintf(fp, "%d\n", (int)getpid());
        fclose(fp);
    }
}
