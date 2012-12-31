#include <Python2.7/Python.h>
#include "application.h"

/* Find occurrence of a character within our buffers */
static int InputStream_findChar(InputStream *self, int start, int c)
{
    const char *data = &self->req_data[start];

    char *found = memchr(data, c, self->size-start);
    if (found != NULL) {
        return (int)(found - data);
    }

    return -1;
}

/* Consume characters between self->pos and newPos, returning it as a
   new Python string. */
static PyObject *InputStream_consume(InputStream *self, int new_pos)
{
    PyObject *result;
    int mem_size = new_pos - self->pos;
    char *data;
    if (!mem_size)
        return PyString_FromString("");

    result = PyString_FromStringAndSize(NULL, mem_size);
    if (result == NULL)
        return NULL;

    data = PyString_AS_STRING(result);

    memcpy(data, self->req_data, mem_size);

    self->pos += mem_size;

    data[mem_size] = '\0';

    /* Free fully-consumed chunks */
    return result;
}

static void InputStream_dealloc(InputStream *self)
{
    PyObject *tmp;

    tmp = (PyObject *)self->response;
    self->response = NULL;
    Py_XDECREF(tmp);

    self->ob_type->tp_free((PyObject *)self);
}

static PyObject *InputStream_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    InputStream *self;

    self = (InputStream *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->response = NULL;
        self->pos = 0;
        self->size = 0;
        self->req_data = NULL;
    }

    return (PyObject *)self;
}

/* InputStream constructor. Expects to be passed the parent Request and
   the received Content-Length. */
static int InputStream_init(InputStream *self, PyObject *args, PyObject *kwds)
{
    struct response *response;
    PyObject *body_obj;
    int size;

    if (!PyArg_ParseTuple(args, "O!O!i", &responseType, &response, &PyCObject_Type, &body_obj, &size))
        return -1;
    char *body = PyCObject_AsVoidPtr(body_obj);

    Py_INCREF(response);
    self->response = response;
    self->req_data = body;
    self->size = size;
    return 0;
}

/* read() implementation */
static PyObject *InputStream_read(InputStream *self, PyObject *args)
{
    int size = -1;

    if (!PyArg_ParseTuple(args, "|i:read", &size))
        return NULL;

    if (self->pos == self->size || size <= 0)
        return PyString_FromString("");

    size = self->size - self->pos > size ? size : self->size - self->pos;

    return InputStream_consume(self, self->pos+size);
}

/* readline() implementation. Supports "size" argument not required by
   WSGI spec (but now required by Python 2.5's cgi module) */
static PyObject *InputStream_readline(InputStream *self, PyObject *args)
{
    int size = -1, start, i, new_pos;

    if (!PyArg_ParseTuple(args, "|i:readline", &size))
        return NULL;

    if (self->pos == self->size || size <= 0)
        return PyString_FromString("");

    size = self->size - self->pos > size ? size : self->size - self->pos;

    start = self->pos;
    /* Find newline */
    i = InputStream_findChar(self, start, '\n');
    if (i < 0) {
        new_pos = self->pos + size;
    } else {
        /* Trim line, if necessary */
        new_pos = size < i+1 ? size : i+1;
    }

    return InputStream_consume(self, new_pos);
}

/* readlines() implementation. Supports "hint" argument. */
static PyObject *InputStream_readlines(InputStream *self, PyObject *args)
{
    int hint = 0, total = 0;
    PyObject *lines = NULL, *args2 = NULL, *line;
    ssize_t len, ret;

    if (!PyArg_ParseTuple(args, "|i:readlines", &hint))
        return NULL;

    if ((lines = PyList_New(0)) == NULL)
        return NULL;

    if ((args2 = PyTuple_New(0)) == NULL)
        goto bad;

    if ((line = InputStream_readline(self, args2)) == NULL)
        goto bad;

    while ((len = PyString_GET_SIZE(line)) > 0) {
        ret = PyList_Append(lines, line);
        Py_DECREF(line);
        if (ret)
            goto bad;
        total += len;
        if (hint > 0 && total >= hint)
            break;

        if ((line = InputStream_readline(self, args2)) == NULL)
            goto bad;
    }

    Py_DECREF(line);
    Py_DECREF(args2);

    return lines;

bad:
    Py_XDECREF(args2);
    Py_XDECREF(lines);
    return NULL;
}

/* __iter__() implementation. Simply returns self. */
static PyObject *InputStream_iter(InputStream *self)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

/* next() implementation for iteration protocol support */
static PyObject *InputStream_iternext(InputStream *self)
{
    PyObject *line, *args;

    if ((args = PyTuple_New(0)) == NULL)
        return NULL;

    line = InputStream_readline(self, args);
    Py_DECREF(args);
    if (line == NULL)
        return NULL;

    if (PyString_GET_SIZE(line) == 0) {
        Py_DECREF(line);
        PyErr_Clear();
        return NULL;
    }

    return line;
}

static PyMethodDef InputStream_methods[] = {
    {"read", (PyCFunction)InputStream_read, METH_VARARGS,
        "Read from this input stream" },
    {"readline", (PyCFunction)InputStream_readline, METH_VARARGS,
        "Read a line from this input stream" },
    {"readlines", (PyCFunction)InputStream_readlines, METH_VARARGS,
        "Read lines from this input stream" },
    { NULL }
};

PyTypeObject InputStream_Type = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "_wsgisup.InputStream",    /*tp_name*/
    sizeof(InputStream),       /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)InputStream_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_ITER, /*tp_flags*/
    "wsgi.input implementation", /* tp_doc */
    0,		             /* tp_traverse */
    0,		             /* tp_clear */
    0,		             /* tp_richcompare */
    0,		             /* tp_weaklistoffset */
    (getiterfunc)InputStream_iter, /* tp_iter */
    (iternextfunc)InputStream_iternext, /* tp_iternext */
    InputStream_methods,       /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)InputStream_init, /* tp_init */
    0,                         /* tp_alloc */
    InputStream_new,           /* tp_new */
};

