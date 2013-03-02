#ifndef WHEATSERVER_UTIL_H
#define WHEATSERVER_UTIL_H

/* Hash Type Dict */
struct dictType wstrDictType;
struct configuration;

void nonBlockCloseOnExecPipe(int *fd0, int *fd1);

/* Configuration Validator */
int stringValidator(struct configuration *conf, const char *key, const char *val);
int unsignedIntValidator(struct configuration *conf, const char *key, const char *val);
int enumValidator(struct configuration *conf, const char *key, const char *val);
int boolValidator(struct configuration *conf, const char *key, const char *val);

/* Assert */
void wheat_stacktrace(int skip_count);
void wheat_assert(const char *cond, const char *file, int line, int panic);

int daemonize(int coredump);
void createPidFile();
void setProctitle(const char *title);
int getFileSize(int fd, off_t *len);
int isRegFile(const char *path);
int fromSameParentDir(wstr left, wstr right);

void setTimer(int milliseconds);

#define ASSERT(_x) do {                            \
    if (!(_x)) {                                   \
        wheat_assert(#_x, __FILE__, __LINE__, 1);  \
    }                                              \
} while (0)

#endif


/* Check if we can use setproctitle().
 * BSD systems have support for it, we provide an implementation for
 * Linux and osx. */
#if (defined __NetBSD__ || defined __FreeBSD__ || defined __OpenBSD__)
#define USE_SETPROCTITLE
#endif

#if (defined __linux || defined __APPLE__)
#define USE_SETPROCTITLE
void setproctitle(const char *fmt, ...);
#endif
