// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <Python.h>
#include "../../slice.h"
#include "app_wsgi.h"

/* Find occurrence of a character within our buffers */
static int InputStream_findChar(InputStream *self, struct slice *s, int c)
{
    uint8_t *found = memchr(s->data, c, s->len);
    if (found != NULL) {
        return (int)(found - s->data);
    }

    return -1;
}

/* Consume characters between self->pos and newPos, returning it as a
   new Python string. */
static PyObject *InputStream_consume(InputStream *self, int size)
{
    PyObject *result;
    char *data;
    size_t total = 0;

    if (size <= 0 || !self->curr)
        return PyString_FromString("");
    result = PyString_FromStringAndSize(NULL, size);
    data = PyString_AS_STRING(result);
    do {
        if (self->pos >= self->curr->len) {
            self->curr = httpGetBodyNext(self->c);
            self->pos = 0;
        }
        size_t remaining = self->curr->len - self->pos;
        size_t copyed = size > remaining ? remaining : size;
        memcpy(data+total, self->curr->data+self->pos, copyed);
        size -= copyed;
        total += copyed;
        self->pos += copyed;
    } while(size > 0);
    self->readed += total;
    return result;
}

static void InputStream_dealloc(InputStream *self)
{
    self->ob_type->tp_free((PyObject *)self);
}

static PyObject *InputStream_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    InputStream *self;

    self = (InputStream *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->c = NULL;
        self->pos = 0;
        self->readed = 0;
    }

    return (PyObject *)self;
}

/* InputStream constructor. Expects to be passed the parent Request and
   the received Content-Length. */
static int InputStream_init(InputStream *self, PyObject *args, PyObject *kwds)
{
    PyObject *c;

    if (!PyArg_ParseTuple(args, "O!", &PyCObject_Type, &c))
        return -1;

    self->c = PyCObject_AsVoidPtr(c);
    self->curr = httpGetBodyNext(self->c);
    return 0;
}

/* read() implementation */
static PyObject *InputStream_read(InputStream *self, PyObject *args)
{
    int size = -1;
    long remaining;

    if (!PyArg_ParseTuple(args, "|i:read", &size))
        return NULL;

    if (size <= 0 || !self->curr)
        return PyString_FromString("");

    remaining = httpBodyGetSize(self->c) - self->readed;
    size = remaining > size ? size : remaining;

    return InputStream_consume(self, size);
}

/* readline() implementation. Supports "size" argument not required by
   WSGI spec (but now required by Python 2.5's cgi module) */
static PyObject *InputStream_readline(InputStream *self, PyObject *args)
{
    long size = -1;
    size_t remaining;

    if (!PyArg_ParseTuple(args, "|i:readline", &size))
        return NULL;

    if (size <= 0)
        return PyString_FromString("");

    remaining = self->curr->len - self->pos + httpBodyGetSize(self->c);
    size = remaining > size ? size : remaining;

    return InputStream_consume(self, size);
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

