#ifndef WHEATSERVER_UTIL_H
#define WHEATSERVER_UTIL_H

/* Hash Type Dict */
struct dictType;
struct configuration;
extern struct dictType wstrDictType;
extern struct dictType sliceDictType;

void nonBlockCloseOnExecPipe(int *fd0, int *fd1);

int daemonize(int coredump);
void createPidFile();
void setProctitle(const char *title);
int getFileSizeAndMtime(int fd, off_t *len, time_t *m_time);
int isRegFile(const char *path);
int fromSameParentDir(wstr left, wstr right);

void setTimer(int milliseconds);

#endif
