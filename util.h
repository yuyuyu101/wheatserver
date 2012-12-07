#ifndef _UTIL_H
#define _UTIL_H

/* Hash Type Dict */
extern struct dictType wstrDictType;

void nonBlockCloseOnExecPipe(int *fd0, int *fd1);

#endif
