// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <Python.h>
#include "app_wsgi.h"

static void FileWrapper_dealloc(FileWrapper *self)
{
    PyObject *tmp;

    tmp = (PyObject *)self->filelike;
    self->filelike = NULL;
    Py_XDECREF(tmp);

    self->ob_type->tp_free((PyObject *)self);
}

    static PyObject *
FileWrapper_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    FileWrapper *self;

    self = (FileWrapper *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->filelike = NULL;
    }

    return (PyObject *)self;
}

    static int
FileWrapper_init(FileWrapper *self, PyObject *args, PyObject *kwds)
{
    PyObject *filelike;
    int blocksize = 4096;

    if (!PyArg_ParseTuple(args, "O|i", &filelike, &blocksize))
        return -1;

    Py_INCREF(filelike);
    self->filelike = filelike;
    self->blocksize = blocksize;

    return 0;
}

/* __iter__() implementation. Simply returns self. */
    static PyObject *
FileWrapper_iter(FileWrapper *self)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

/* next() implementation for iteration protocol support. Calls read()
   on the file-like and emits strings according to the blocksize.
   This is only used for compatibility (the normal path uses
   wsgiSendFile()). */
static PyObject *
FileWrapper_iternext(FileWrapper *self)
{
    PyObject *pRead, *args, *data;
    long len;

    if ((pRead = PyObject_GetAttrString(self->filelike, "read")) == NULL)
        return NULL;

    if ((args = Py_BuildValue("(i)", self->blocksize)) == NULL) {
        Py_DECREF(pRead);
        return NULL;
    }

    data = PyObject_CallObject(pRead, args);
    Py_DECREF(args);
    Py_DECREF(pRead);
    if (data == NULL)
        return NULL;

    len = PyString_Size(data);
    if (PyErr_Occurred()) {
        Py_DECREF(data);
        return NULL;
    }

    if (len == 0) {
        Py_DECREF(data);
        PyErr_Clear();
        return NULL;
    }

    return data;
}

/* Calls the file-like's close() method, if present. */
    static PyObject *
FileWrapper_close(FileWrapper *self, PyObject *args)
{
    PyObject *pClose, *args2, *result;

    if (PyObject_HasAttrString(self->filelike, "close")) {
        if ((pClose = PyObject_GetAttrString(self->filelike, "close")) == NULL)
            return NULL;

        if ((args2 = PyTuple_New(0)) == NULL) {
            Py_DECREF(pClose);
            return NULL;
        }

        result = PyObject_CallObject(pClose, args2);
        Py_DECREF(args2);
        Py_DECREF(pClose);
        if (result == NULL)
            return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef FileWrapper_methods[] = {
    { "close", (PyCFunction)FileWrapper_close, METH_VARARGS,
        "Calls the file-like object's close method" },
    { NULL }
};

PyTypeObject FileWrapper_Type = {
    PyObject_HEAD_INIT(NULL)
        0,                         /*ob_size*/
    "_wsgisup.FileWrapper",    /*tp_name*/
    sizeof(FileWrapper),       /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)FileWrapper_dealloc, /*tp_dealloc*/
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
    "wsgi.file_wrapper implementation", /* tp_doc */
    0,		             /* tp_traverse */
    0,		             /* tp_clear */
    0,		             /* tp_richcompare */
    0,		             /* tp_weaklistoffset */
    (getiterfunc)FileWrapper_iter, /* tp_iter */
    (iternextfunc)FileWrapper_iternext, /* tp_iternext */
    FileWrapper_methods,       /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)FileWrapper_init, /* tp_init */
    0,                         /* tp_alloc */
    FileWrapper_new,           /* tp_new */
};
