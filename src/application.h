#ifndef WHEATSERVER_APPLICANT_H
#define WHEATSERVER_APPLICANT_H

struct app *spotAppInterface();
void initWsgi();
void deallocWsgi();
int wsgiConstructor(struct client *);
void *initWsgiAppData();
void freeWsgiAppData(void *app_data);

#endif
