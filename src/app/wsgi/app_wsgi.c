#include "../application.h"
#include "app_wsgi.h"
#include "../static/app_static_file.h"
#include "../../protocol/http/proto_http.h"

static PyObject *pApp = NULL;
static PyObject *WsgiStderr = NULL;

int wsgiCall(struct client *client, void *arg)
{
    /* Create Request object, passing it the context as a CObject */
    int is_ok = 1;
    PyObject *start_resp, *result, *args, *env;
    struct response *req_obj = NULL;
    PyObject *res = PyCObject_FromVoidPtr(client, NULL);
    if (res == NULL)
        goto out;

    env = createEnviron(client);
    if (env == NULL)
        goto out;

    args = Py_BuildValue("(OO)", res, env);
    Py_DECREF(res);
    if (args == NULL)
        goto out;

    /* env now owned by req_obj */
    req_obj = (struct response *)PyObject_CallObject((PyObject *)&responseType, args);
    Py_DECREF(args);
    if (req_obj == NULL)
        goto out;

    /* Get start_response callable */
    start_resp = PyObject_GetAttrString((PyObject *)req_obj, "start_response");
    if (start_resp == NULL)
        goto out;

    /* Build arguments and call application object */
    args = Py_BuildValue("(OO)", env, start_resp);
    Py_DECREF(start_resp);
    if (args == NULL)
        goto out;

    result = PyObject_CallObject(pApp, args);
    Py_DECREF(args);
    if (result != NULL) {
        /* result now owned by req_obj */
        req_obj->result = result;

        /* Handle the application response */
        wsgiSendResponse(req_obj, result); /* ignore return */
        wsgiCallClose(result);
    }

out:
    if (PyErr_Occurred()) {
        PyErr_Print();

        /* Display HTTP 500 error, if possible */
        struct httpData *http_data = client->protocol_data;
        if (req_obj == NULL || !http_data->headers_sent)
            sendResponse500(client);
        is_ok = 0;
    }

    if (req_obj != NULL) {
        /* Don't rely on cyclic GC. Clear circular references NOW. */
        responseClear(req_obj);
        Py_DECREF(req_obj);
    }

    logAccess(client);
    if (is_ok)
        return WHEAT_OK;
    return WHEAT_WRONG;
}

void *initWsgiAppData(struct client *c)
{
    struct wsgiData *data = malloc(sizeof(struct wsgiData));
    if (data == NULL)
        return NULL;
    data->environ = NULL;
    data->response = NULL;
    data->err = NULL;

    return data;
}

void freeWsgiAppData(void *data)
{
    struct wsgiData *d = data;
    free(d);
}

void initWsgi()
{
    char *app_t;
    char buf[WHEATSERVER_PATH_LEN];
    struct configuration *conf;
    Py_Initialize();

    conf = getConfiguration("app-project-path");
    snprintf(buf, WHEATSERVER_PATH_LEN, "import sys, os\n"
            "sys.path.append(os.getcwd())\n"
            "sys.path.append('%s')" , conf->target.ptr);
    PyRun_SimpleString(buf);

    init_wsgisup();
    conf = getConfiguration("app-module-name");
    app_t = conf->target.ptr;

    if (!app_t)
        goto err;
    PyObject *pModule, *pName = PyString_FromString(app_t);
    if (pName == NULL)
        goto err;

    pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    if (pModule == NULL)
        goto err;

    conf = getConfiguration("app-name");
    app_t = conf->target.ptr;
    if (!app_t)
        goto err;
    pApp = PyObject_GetAttrString(pModule, app_t);
    Py_DECREF(pModule);
    if (pApp == NULL || !PyCallable_Check(pApp)) {
        Py_XDECREF(pApp);
        goto err;
    }

    pName = PyString_FromString("sys");
    if (pName == NULL)
        goto err;
    pModule = PyImport_Import(pName);
    if (pModule == NULL)
        goto err;

    WsgiStderr = PyObject_GetAttrString(pModule, "stderr");
    Py_DECREF(pName);
    Py_DECREF(pModule);

    return ;
err:
    PyErr_Print();
    wheatLog(WHEAT_WARNING, "initWsgi failed");
    halt(1);
}

void deallocWsgi()
{
    Py_DECREF(pApp);
    Py_DECREF(WsgiStderr);
    Py_Finalize();
}

int envPutString(PyObject *dict, const void *key, const void *value)
{
    PyObject *val;
    int ret;

    if ((val = PyString_FromString(value)) == NULL)
        return -1;
    ret = PyDict_SetItemString(dict, key, val);
    Py_DECREF(val);
    if (ret)
        return -1;
    return 0;
}

/* Assumes c is a valid hex digit */
static inline int toxdigit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/* Unquote an escaped path */
const char *wsgiUnquote(const char *s)
{
    size_t len = strlen(s);
    char *result, *t;

    if ((result = malloc(len + 1)) == NULL)
        return NULL;

    t = result;
    while (*s) {
        if (*s == '%') {
            if (s[1] && s[2] && isxdigit(s[1]) && isxdigit(s[2])) {
                *(t++) = (toxdigit(s[1]) << 4) | toxdigit(s[2]);
                s += 3;
            }
            else
                *(t++) = *(s++);
        }
        else
            *(t++) = *(s++);
    }
    *t = '\0';

    return result;
}

static PyObject *defaultEnviron(PyObject *env, struct httpData *http_data)
{
    PyObject *val;
    if (PyDict_SetItemString(env, "wsgi.errors", WsgiStderr) != 0)
        return NULL;
    if ((val = Py_BuildValue("(ii)", 1, 0)) == NULL)
        return NULL;
    if (PyDict_SetItemString(env, "wsgi.version", val) != 0)
        return NULL;
    Py_DECREF(val);
    if (PyDict_SetItemString(env, "wsgi.multiprocess", Server.worker_number > 1 ? Py_True: Py_False) != 0)
        return NULL;
    if (PyDict_SetItemString(env, "wsgi.multithread", Py_False) != 0)
        return NULL;
    if (PyDict_SetItemString(env, "wsgi.run_once", Py_False) != 0)
        return NULL;

    if (envPutString(env, "wsgi.url_scheme", http_data->url_scheme))
        return NULL;

    if (envPutString(env, "REQUEST_METHOD", http_data->method))
        return NULL;

    if (envPutString(env, "SERVER_PROTOCOL", http_data->protocol_version))
        return NULL;

    if (envPutString(env, "QUERY_STRING", http_data->query_string))
        return NULL;

    return env;
}

PyObject *createEnviron(struct client *client)
{
    struct httpData *http_data = client->protocol_data;
    const char *req_uri = NULL;
    PyObject *environ;
    environ = PyDict_New();
    char buf[256];
    int result = 1;

    if (!environ)
        return NULL;
    environ = defaultEnviron(environ, http_data);
    if (environ == NULL) {
        Py_DECREF(environ);
        return NULL;
    }

    /* HTTP headers */
    struct dictIterator *iter = dictGetIterator(http_data->req_headers);
    struct dictEntry *entry;
    wstr host = NULL, port = NULL, server = NULL;
    while ((entry = dictNext(iter)) != NULL) {
        int k, j;
        wstr header = dictGetKey(entry);
        wstr value = dictGetVal(entry);

        /* Copy/convert header name */
        strcpy(buf, "HTTP_");

        k = wstrlen(header);
        /* The length of header field should less than 250 */
        if (k > (sizeof(buf) - 6))
            k = sizeof(buf) - 6;

        for (j = 0; j < k; j++) {
            if (header[j] == '-')
                buf[5 + j] = '_';
            else
                buf[5 + j] = toupper(header[j]);
        }
        buf[5 + j] = '\0';

        if (!strcmp(buf, "HTTP_CONTENT_TYPE") ||
                !strcmp(buf, "HTTP_CONTENT_LENGTH")) {
            /* Strip HTTP_ */
            if (envPutString(environ, &buf[5], value))
                goto cleanup;
        } else if (!strcmp(buf, "HTTP_X_FORWARDED_FOR")) {
            if (envPutString(environ, buf, value))
                goto cleanup;
            parserForward(value, &host, &port);
        } else if (!strcmp(buf, "HTTP_EXPECT")) {
            if (envPutString(environ, buf, value))
                goto cleanup;
            wstrLower(value);
            if (!strcmp(value, "100-continue")) {
                // No need to free `h`
                wstr h = wstrNew("HTTP/1.1 100 Continue\r\n\r\n");
                WorkerProcess->worker->sendData(client, h);
            }
        } else if (!strcmp(buf, "HTTP_HOST")) {
            if (envPutString(environ, buf, value))
                goto cleanup;
            server = wstrDup(value);
        } else if (!strcmp(buf, "HTTP_SCRIPT_NAME")) {
            if (envPutString(environ, "SCRIPT_NAME", value))
                goto cleanup;
        } else {
            if (envPutString(environ, buf, value))
                goto cleanup;
        }
    }
    dictReleaseIterator(iter);
    if (!host) {
        host = wstrNew(client->ip);
        snprintf(buf, 255, "%d", client->port);
        port = wstrNew(buf);
    }
    if (envPutString(environ, "REMOTE_ADDR", host))
        goto cleanup;
    if (envPutString(environ, "REMOTE_PORT", port))
        goto cleanup;
    char *sep = strchr(server, ':');
    if (sep) {
        if (envPutString(environ, "SERVER_PORT", sep+1))
            goto cleanup;
        server = wstrRange(server, (int)(sep-server+1), 0);
    } else if (!strcmp(http_data->url_scheme, "HTTP")) {
        if (envPutString(environ, "SERVER_PORT", "80"))
            goto cleanup;
    } else if (!strcmp(http_data->url_scheme, "HTTPS")) {
        if (envPutString(environ, "SERVER_PORT", "443"))
            goto cleanup;
    }
    if (envPutString(environ, "SERVER_NAME", server))
        goto cleanup;

    if ((req_uri = wsgiUnquote(http_data->path)) == NULL) {
        goto cleanup;
    }
    if (envPutString(environ, "PATH_INFO", req_uri))
        goto cleanup;
    result = 0;

cleanup:
    if (req_uri)
        free((void *)req_uri);
    wstrFree(host);
    wstrFree(port);

    if (server)
        wstrFree(server);
    if (result) {
        Py_DECREF(environ);
        return NULL;
    }
    return environ;
}


void responseClear(struct response *self)
{
    PyObject *tmp;

    tmp = self->headers;
    self->headers = NULL;
    Py_XDECREF(tmp);

    tmp = self->result;
    self->result = NULL;
    Py_XDECREF(tmp);

    tmp = self->env;
    self->env = NULL;
    Py_XDECREF(tmp);

    tmp = self->input;
    self->input = NULL;
    Py_XDECREF(tmp);
}

void wsgiCallClose(PyObject *result)
{
    PyObject *type, *value, *traceback;
    PyObject *pClose, *args, *ret;

    /* Save exception state */
    PyErr_Fetch(&type, &value, &traceback);

    if (PyObject_HasAttrString(result, "close")) {
        pClose = PyObject_GetAttrString(result, "close");
        if (pClose != NULL) {
            args = PyTuple_New(0);
            if (args != NULL) {
                ret = PyObject_CallObject(pClose, args);
                Py_DECREF(args);
                Py_XDECREF(ret);
            }
            Py_DECREF(pClose);
        }
    }

    /* Restore exception state */
    PyErr_Restore(type, value, traceback);
}

static void responseDealloc(struct response *self)
{
    responseClear(self);
    self->ob_type->tp_free((PyObject *)self);
}

static PyObject * responseNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    struct response *self;

    self = (struct response *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->headers = NULL;
        self->result = NULL;
        self->env = NULL;
        self->input = NULL;
    }

    return (PyObject *)self;
}

/* Constructor. Accepts the context CObject as its sole argument. */
static int responseInit(struct response *self, PyObject *args, PyObject *kwds)
{
    PyObject *context_obj, *args2, *env, *body_obj;
    struct httpData *http_data;

    if (!PyArg_ParseTuple(args, "O!O!", &PyCObject_Type, &context_obj, &PyDict_Type, &env))
        return -1;

    self->client = PyCObject_AsVoidPtr(context_obj);

    // Build InputStream Object
    http_data = self->client->protocol_data;
    body_obj = PyCObject_FromVoidPtr(http_data->body, NULL);
    if ((args2 = Py_BuildValue("(OOi)", self, body_obj, wstrlen(http_data->body))) == NULL)
        return -1;
    self->input = PyObject_CallObject((PyObject *)&InputStream_Type, args2);
    Py_DECREF(args2);
    if (self->input == NULL)
        return -1;

    // Add More WSGI Variable To Env
    self->env = env;
    if (PyDict_SetItemString(env, "wsgi.input", self->input) != 0)
        return -1;
    return 0;
}

/* start_response() callable implementation */
static PyObject * startResponse(struct response *self, PyObject *args)
{
    PyObject *status, *headers, *exc_info = NULL;
    struct wsgiData *data = self->client->app_private_data;
    struct httpData *http_data = self->client->protocol_data;
    const char *status_p;
    int response_length = 0;

    if (!PyArg_ParseTuple(args, "SO!|O:start_response", &status,
                &PyList_Type, &headers,
                &exc_info))
        return NULL;

    if (exc_info != NULL && exc_info != Py_None) {
        /* If the headers have already been sent, just propagate the
           exception. */
        if (http_data->headers_sent) {
            PyObject *type, *value, *tb;
            if (!PyArg_ParseTuple(exc_info, "OOO", &type, &value, &tb))
                return NULL;
            Py_INCREF(type);
            Py_INCREF(value);
            Py_INCREF(tb);
            PyErr_Restore(type, value, tb);
            return NULL;
        }
    } else if (http_data->res_status != 0) {
        data->err = "headers already set";
        return NULL;
    }

    PyObject *iterator = PyObject_GetIter(headers);
    PyObject *item;

    if (iterator == NULL) {
        data->err = "headers is NULL";
        return NULL;
    }

    char buf[256];
    while ((item = PyIter_Next(iterator)) != NULL) {
        char *field, *value;
        int ret;
        if (!PyArg_ParseTuple(item, "ss", &field, &value)) {
            Py_DECREF(item);
            Py_DECREF(iterator);
            return NULL;
        }
        if (!strcasecmp(field, "content-length"))
            response_length = atoi(value);
        else if (!strcasecmp(field, "connection")){
            if (!strcasecmp(value, "upgrade"))
                http_data->upgrade = 1;
        }
        ret = snprintf(buf, 255, "%s: %s\r\n", field, value);
        if (ret >= 255) {
            wheatLog(WHEAT_NOTICE, "may overflow %s", buf);
        }
        appendToListTail(http_data->res_headers, wstrNew(buf));
        Py_DECREF(item);
    }

    Py_DECREF(iterator);

    if (PyErr_Occurred()) {
        PyErr_Print();
        return NULL;
    }

    /* TODO validation of status and headers */
    if ((status_p = PyString_AsString(status)) == NULL)
        return NULL;

    fillResInfo(http_data, response_length,
            (int)strtol(status_p, NULL, 10), &status_p[4]);

    return PyObject_GetAttrString((PyObject *)self, "write");
}

/* write() callable implementation */
static PyObject *responseWrite(struct response *self, PyObject *args)
{
    struct wsgiData *wsgi_data = self->client->app_private_data;
    struct httpData *http_data = self->client->protocol_data;
    const char *data;
    int dataLen;

    if (http_data->res_status == 0 && http_data->res_headers == NULL) {
        wsgi_data->err = "write() before start_response()";
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "s#:write", &data, &dataLen))
        return NULL;

    /* Send headers if necessary */
    if (!http_data->headers_sent) {
        if (httpSendHeaders(self->client))
            return NULL;
    }

    if (httpSendBody(self->client, data, dataLen)) {
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef response_method[] = {
    { "start_response", (PyCFunction)startResponse, METH_VARARGS,
        "WSGI start_response callable" },
    { "write", (PyCFunction)responseWrite, METH_VARARGS,
        "WSGI write callable" },
    { NULL }
};

PyTypeObject responseType = {
    PyObject_HEAD_INIT(NULL)
        0,                         /*ob_size*/
    "_wsgisup.Request",        /*tp_name*/
    sizeof(struct response),           /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)responseDealloc, /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "WSGI Request class",      /* tp_doc */
    0,		             /* tp_traverse */
    0,		             /* tp_clear */
    0,		             /* tp_richcompare */
    0,		             /* tp_weaklistoffset */
    0,		             /* tp_iter */
    0,		             /* tp_iternext */
    response_method,           /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)responseInit,    /* tp_init */
    0,                         /* tp_alloc */
    responseNew,               /* tp_new */
};

static PyMethodDef wsgisup_methods[] = {
    { NULL }
};

#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
init_wsgisup(void)
{
    PyObject *m;

    if (PyType_Ready(&responseType) < 0)
        return;
    if (PyType_Ready(&FileWrapper_Type) < 0)
        return;
    if (PyType_Ready(&InputStream_Type) < 0)
        return;

    m = Py_InitModule3("_wsgisup", wsgisup_methods,
            "WSGI C support module");

    if (m == NULL)
        return;

    Py_INCREF(&responseType);
    PyModule_AddObject(m, "Request", (PyObject *)&responseType);
    Py_INCREF(&FileWrapper_Type);
    PyModule_AddObject(m, "FileWrapper", (PyObject *)&FileWrapper_Type);
    Py_INCREF(&InputStream_Type);
    PyModule_AddObject(m, "InputStream", (PyObject *)&InputStream_Type);
}

/* Send a file as the HTTP response body. */
int wsgiSendFile(struct client *c, int fd)
{
    int ret = 0;

    ret = sendFile(c, fd, 0);

    return ret;
}

/* Send a wrapped file using wsgiSendFile */
static int wsgiSendFileWrapper(struct response *self, FileWrapper *wrapper)
{
    PyObject *pFileno, *args, *pFD;
    int fd;
    struct httpData *http_data = self->client->protocol_data;

    /* file-like must have fileno */
    if (!PyObject_HasAttrString((PyObject *)wrapper->filelike, "fileno"))
        return 1;

    if ((pFileno = PyObject_GetAttrString((PyObject *)wrapper->filelike,
                    "fileno")) == NULL)
        return -1;

    if ((args = PyTuple_New(0)) == NULL) {
        Py_DECREF(pFileno);
        return -1;
    }

    pFD = PyObject_CallObject(pFileno, args);
    Py_DECREF(args);
    Py_DECREF(pFileno);
    if (pFD == NULL)
        return -1;

    fd = (int)PyInt_AsLong(pFD);
    Py_DECREF(pFD);
    if (PyErr_Occurred())
        return -1;

    /* Send headers if necessary */
    if (!http_data->headers_sent) {
        if (httpSendHeaders(self->client))
            return -1;
        http_data->headers_sent = 1;
    }

    if (wsgiSendFile(self->client, fd))
        return -1;

    return 0;
}

/* Send the application's response */
int wsgiSendResponse(struct response *self, PyObject *result)
{
    PyObject *iter;
    PyObject *item;
    int ret = 0;
    struct httpData *http_data = self->client->protocol_data;

    /* Check if it's a FileWrapper */
    if (result->ob_type == &FileWrapper_Type) {
        ret = wsgiSendFileWrapper(self, (FileWrapper *)result);
        if (ret < 0)
            return -1;
        if (!ret)
            return 0;
        /* Fallthrough */
    }

    iter = PyObject_GetIter(result);
    if (iter == NULL)
        return -1;

    while ((item = PyIter_Next(iter))) {
        size_t dataLen;
        const char *data;

        dataLen = PyString_Size(item);
        if (PyErr_Occurred()) {
            Py_DECREF(item);
            break;
        }

        if (dataLen) {
            if ((data = PyString_AsString(item)) == NULL) {
                Py_DECREF(item);
                break;
            }

            /* Send headers if necessary */
            if (!http_data->headers_sent) {
                if (httpSendHeaders(self->client)) {
                    ret = -1;
                    Py_DECREF(item);
                    break;
                }
                http_data->headers_sent = 1;
            }

            if (httpSendBody(self->client, data, dataLen)) {
                ret = -1;
                Py_DECREF(item);
                break;
            }
        }
        Py_DECREF(item);
    }
    Py_DECREF(iter);

    if (PyErr_Occurred())
        return -1;

    /* Send headers if they haven't been sent at this point */
    if (!http_data->headers_sent) {
        if (httpSendHeaders(self->client))
            return -1;
        http_data->headers_sent = 1;
    }
    return ret;
}
