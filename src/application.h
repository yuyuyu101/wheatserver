#ifndef WHEATSERVER_APPLICANT_H
#define WHEATSERVER_APPLICANT_H

int wsgiCall(struct client *, void *);
void initWsgi();
void deallocWsgi();
void *initWsgiAppData();
void freeWsgiAppData(void *app_data);

int staticFileCall(struct client *, void *);
void initStaticFile();
void deallocStaticFile();
void *initStaticFileData();
void freeStaticFileData(void *app_data);

#endif
