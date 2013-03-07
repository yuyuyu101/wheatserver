#ifndef WHEATSERVER_WSGI_H
#define WHEATSERVER_WSGI_H
/* ========== wsgi callback ========== */
#include <Python.h>
#include "../../wstr.h"
#include "../../slice.h"
#include "../../protocol/http/proto_http.h"

struct client;

struct response {
    PyObject_HEAD
        /* All fields are private */
    struct client *client;
    PyObject *headers;
    PyObject *result;
    PyObject *env;
    PyObject *input;
};

typedef struct {
    PyObject_HEAD
    PyObject *filelike;
    int blocksize;
} FileWrapper;

typedef struct {
    PyObject_HEAD
    struct response *response;
    size_t readed;
    const struct slice *curr;
    int pos;
} InputStream;

struct wsgiData {
    PyObject *environ;
    void *response;
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
PyObject *createEnviron(struct client *client);
void responseClear(struct response *);
void wsgiCallClose(PyObject *result);
int wsgiSendResponse(struct response *self, PyObject *result);

#endif
