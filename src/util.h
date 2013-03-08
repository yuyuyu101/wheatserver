#ifndef WHEATSERVER_UTIL_H
#define WHEATSERVER_UTIL_H

/* Hash Type Dict */
struct dictType;
struct configuration;
extern struct dictType wstrDictType;
extern struct dictType sliceDictType;

void nonBlockCloseOnExecPipe(int *fd0, int *fd1);

/* Configuration Validator */
int stringValidator(struct configuration *conf, const char *key, const char *val);
int unsignedIntValidator(struct configuration *conf, const char *key, const char *val);
int enumValidator(struct configuration *conf, const char *key, const char *val);
int boolValidator(struct configuration *conf, const char *key, const char *val);

int daemonize(int coredump);
void createPidFile();
void setProctitle(const char *title);
int getFileSize(int fd, off_t *len);
int isRegFile(const char *path);
int fromSameParentDir(wstr left, wstr right);

void setTimer(int milliseconds);

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
#endif

ssize_t portable_sendfile(int out_fd, int in_fd, off_t len);
