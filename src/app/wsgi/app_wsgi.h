#ifndef WHEATSERVER_WSGI_H
#define WHEATSERVER_WSGI_H
/* ========== wsgi callback ========== */
#include <Python.h>
#include "../../wstr.h"
#include "../../slice.h"
#include "../../protocol/http/proto_http.h"

struct conn;

struct response {
    PyObject_HEAD
        /* All fields are private */
    struct conn *c;
};

typedef struct {
    PyObject_HEAD
    PyObject *filelike;
    int blocksize;
} FileWrapper;

typedef struct {
    PyObject_HEAD
    struct conn *c;
    size_t readed;
    const struct slice *curr;
    int pos;
} InputStream;

struct wsgiData {
    PyObject *environ;
    void *response;
    struct array *body_items;
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
PyObject *createEnviron(struct conn *c);
void wsgiCallClose(PyObject *result);

#endif
