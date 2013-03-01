#ifndef WHEATSERVER_WSGI_H
#define WHEATSERVER_WSGI_H
/* ========== wsgi callback ========== */
#include <Python.h>
#include "wstr.h"

struct client;

struct response {
    PyObject_HEAD
        /* All fields are private */
    struct client *client;
    PyObject *headers;
    PyObject *result;
    PyObject *env;
    PyObject *input;
    int headers_sent;
};

typedef struct {
    PyObject_HEAD
    PyObject *filelike;
    int blocksize;
} FileWrapper;

typedef struct {
    PyObject_HEAD
    struct response *response;
    int pos, size;
    const char *req_data;
} InputStream;

struct wsgiData {
    PyObject *environ;
    void *response;
    int send;
    char *err;
};

PyTypeObject responseType;
PyTypeObject FileWrapper_Type;
PyTypeObject InputStream_Type;

#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
init_wsgisup(void);
PyObject *create_environ(struct client *client);
void sendResponse500(struct response *response);
void responseClear(struct response *);
void wsgiCallClose(PyObject *result);
int wsgiSendResponse(struct response *self, PyObject *result);

#endif
