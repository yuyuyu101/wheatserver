#ifndef WHEATSERVER_APP_STATIC_FILE_H
#define WHEATSERVER_APP_STATIC_FILE_H

int sendFile(struct client *c, int fd, off_t len);

#endif
