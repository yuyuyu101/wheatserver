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
};

typedef struct {
    PyObject_HEAD
    PyObject *filelike;
    int blocksize;
} FileWrapper;

typedef struct {
    PyObject_HEAD
    struct client *client;
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
void wsgiCallClose(PyObject *result);

#endif
