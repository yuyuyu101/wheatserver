/*-
 * Copyright (c) 2006 Allan Saddi <allan@saddi.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 */

#include <python2.7/Python.h>
#include "application.h"

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
