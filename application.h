#ifndef _APPLICANT_H
#define _APPLICANT_H
/* ========== wsgi callback ========== */
#include <python2.7/Python.h>
struct client;

struct response {
    PyObject_HEAD
        /* All fields are private */
    struct client *client;
    PyObject *headers;
    int headers_sent;
};

struct wsgiData {
    PyObject *environ;
    void *response;
    int status;
    int send;
    int response_length;
    struct dict *headers;
    char *err;
    PyObject *pApp;
};

PyTypeObject responseType;

int wsgiConstructor(struct client *);
void *initWsgiAppData();
void freeWsgiAppData(void *app_data);

PyObject *create_environ(struct client *client);
void sendResponse500(struct response *response);
void responseClear(struct response *);
void wsgiCallClose(PyObject *result);

#endif
