#include <python2.7/Python.h>
#include "wheatserver.h"
#include "application.h"

int wsgiPutEnv(PyObject *dict, const void *key, const void *value)
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
    int len = strlen(s);
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

PyObject *default_environ(PyObject *env, struct httpData *http_data)
{
    PyObject *pName, *pModule, *wsgiStderr, *val;
    pName = PyString_FromString("sys");
    if (pName == NULL)
        return NULL;
    pModule = PyImport_Import(pName);
    if (pModule == NULL)
        return NULL;

    wsgiStderr = PyObject_GetAttrString(pModule, "stderr");
    Py_DECREF(pName);
    Py_DECREF(pModule);
    if (wsgiStderr == NULL)
        return NULL;
    if (wsgiPutEnv(env, "wsgi.errors", wsgiStderr))
        return NULL;
    if ((val = Py_BuildValue("(ii)", 1, 0)) == NULL)
        return NULL;
    if (wsgiPutEnv(env, "wsgi.version", val))
        return NULL;
    Py_DECREF(val);
    if (wsgiPutEnv(env, "wsgi.multiprocess", Server.worker_number > 1 ? Py_True: Py_False))
        return NULL;
    if (wsgiPutEnv(env, "wsgi.multithread", Py_False))
        return NULL;
    if (wsgiPutEnv(env, "wsgi.run_once", Py_False))
        return NULL;

    if (wsgiPutEnv(env, "wsgi.url_scheme", http_data->url_scheme))
        return NULL;

    //    if (wsgiPutEnv(env, "wsgi.file_wrapper", NULL))
    //        return NULL;
    if (wsgiPutEnv(env, "REQUEST_METHOD", http_data->method))
        return NULL;

    if (wsgiPutEnv(env, "SERVER_PROTOCOL", http_data->protocol_version))
        return NULL;

    if (wsgiPutEnv(env, "QUERY_STRING", http_data->query_string))
        return NULL;

    return env;
}

PyObject *create_environ(struct client *client)
{
    struct httpData *http_data = client->protocol_data;
    const char *req_uri;
    PyObject *environ;
    environ = PyDict_New();
    char buf[256];
    int result = 1;

    if (environ)
        return NULL;
    environ = default_environ(environ, http_data);
    if (environ == NULL)
        goto cleanup;

    /* HTTP headers */
    struct dictIterator *iter = dictGetIterator(http_data->headers);
    struct dictEntry *entry;
    wstr host = NULL, port, server = NULL, script_name = NULL;
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
            if (wsgiPutEnv(environ, &buf[5], value))
                goto cleanup;
        } else if (!strcmp(buf, "HTTP_X_FORWARDED_FOR")) {
            if (wsgiPutEnv(environ, buf, value))
                goto cleanup;
            parserForward(value, &host, &port);
        } else if (!strcmp(buf, "HTTP_EXPECT")) {
            if (wsgiPutEnv(environ, buf, value))
                goto cleanup;
            wstrLower(value);
            wstr expect = wstrNew("HTTP/1.1 100 Continue\r\n\r\n");
            if (!strcmp(value, "100-continue"))
                writeBulkTo(client->clifd, &expect);
        } else if (!strcmp(buf, "HTTP_HOST")) {
            if (wsgiPutEnv(environ, buf, value))
                goto cleanup;
            server = wstrDup(value);
        } else if (!strcmp(buf, "HTTP_SCRIPT_NAME")) {
            script_name = wstrNew(value);
            if (wsgiPutEnv(environ, "SCRIPT_NAME", ""))
                goto cleanup;
        } else {
            if (wsgiPutEnv(environ, buf, value))
                goto cleanup;
        }
    }
    dictReleaseIterator(iter);
    if (!host) {
        host = client->ip;
        snprintf(buf, 255, "%d", client->port);
        port = wstrNew(buf);
    }
    if (wsgiPutEnv(environ, "REMOTE_ADDR", host))
        goto cleanup;
    if (wsgiPutEnv(environ, "REMOTE_PORT", port))
        goto cleanup;
    char *sep = strchr(server, ':');
    if (sep) {
        if (wsgiPutEnv(environ, "SERVER_PORT", sep+1))
            goto cleanup;
        server = wstrRange(server, sep-server+1, -1);
    } else if (!strcmp(http_data->url_scheme, "HTTP")) {
        if (wsgiPutEnv(environ, "SERVER_PORT", "80"))
            goto cleanup;
    } else if (!strcmp(http_data->url_scheme, "HTTPS")) {
        if (wsgiPutEnv(environ, "SERVER_PORT", "443"))
            goto cleanup;
    }
    if (wsgiPutEnv(environ, "SERVER_NAME", server))
        goto cleanup;

    if ((req_uri = wsgiUnquote(http_data->path)) == NULL) {
        goto cleanup;
    }
    if (wsgiPutEnv(environ, "PATH_INFO", req_uri))
        goto cleanup;
    result = 0;

cleanup:
    Py_DECREF(environ);
    free((void *)req_uri);
    if (!host) {
        wstrFree(host);
        wstrFree(port);
    }

    if (server)
        wstrFree(server);
    if (result)
        return NULL;
    return environ;
}
struct list *createResHeader(struct client *client)
{
    struct wsgiData *wsgi_data = client->app_private_data;
    struct httpData *http_data = client->protocol_data;
    char *connection = NULL;
    char buf[256];
    struct list *headers = createList();
    if (wsgi_data->upgrade)
        connection = "upgrade";
    else if (http_data->keep_live)
        connection = "close";
    else
        connection = "keep-live";

    listSetFree(headers, (void (*)(void *))wstrFree);

    snprintf(buf, 255, "%s %d\r\n", http_data->protocol_version, wsgi_data->status);
    if (appendToListTail(headers, wstrNew(buf)) == NULL)
        goto cleanup;
    snprintf(buf, 255, "Server: %s\r\n", Server.master_name);
    if (appendToListTail(headers, wstrNew(buf)) == NULL)
        goto cleanup;
    snprintf(buf, 255, "Date: %s\r\n", httpDate());
    if (appendToListTail(headers, wstrNew(buf)) == NULL)
        goto cleanup;
    snprintf(buf, 255, "Connection: %s\r\n", connection);
    if (appendToListTail(headers, wstrNew(buf)) == NULL)
        goto cleanup;

    if (is_chunked(wsgi_data->response_length, http_data->protocol_version, wsgi_data->status)) {
        snprintf(buf, 255, "Transfer-Encoding: chunked\r\n");
        if (appendToListTail(headers, wstrNew(buf)) == NULL)
            goto cleanup;
    }
    return headers;

cleanup:
    freeList(headers);
    return NULL;
}


static int wsgiSendHeaders(struct response *self)
{
    struct client *client = self->client;
    struct wsgiData *wsgi_data = client->app_private_data;

    if (self->headers_sent)
        return 0;
    struct list *headers = createResHeader(client);
    struct dictIterator *iter = dictGetIterator(wsgi_data->headers);
    struct dictEntry *entry = NULL;
    char buf[256];
    while (entry = dictNext(iter)) {
        snprintf(buf, 255, "%s: %s\r\n", (char *)dictGetKey(entry), (char *)dictGetVal(entry));
        if (appendToListTail(headers, wstrNew(buf)) == NULL)
            goto cleanup;
    }

    struct listIterator *liter = listGetIterator(headers, START_HEAD);
    struct listNode *node = NULL;
    while (node = listNext(liter)) {
        client->res_buf = wstrCat(client->res_buf, listNodeValue(node));
        if (client->res_buf == NULL)
            goto cleanup;
    }
    client->res_buf = wstrCat(client->res_buf, "\r\n");
    if (client->res_buf == NULL)
        goto cleanup;
    self->headers_sent = 1;

cleanup:
    dictReleaseIterator(iter);
    freeListIterator(liter);
    freeList(headers);
    return 0;
}

void responseClear(struct response *self)
{
    PyObject *tmp;

    tmp = self->headers;
    self->headers = NULL;
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
        self->headers_sent = 0;
    }

    return (PyObject *)self;
}

/* Constructor. Accepts the context CObject as its sole argument. */
static int responseInit(struct response *self, PyObject *args, PyObject *kwds)
{
    PyObject *context_obj, *args2;

    if (!PyArg_ParseTuple(args, "O!", &PyCObject_Type, &context_obj))
        return -1;

    self->client = PyCObject_AsVoidPtr(context_obj);

    return 0;
}

/* start_response() callable implementation */
static PyObject * startResponse(struct response *self, PyObject *args)
{
    PyObject *status, *headers, *exc_info = NULL;
    struct wsgiData *data = self->client->app_private_data;

    if (!PyArg_ParseTuple(args, "SO!|O:start_response", &status,
                &PyList_Type, &headers,
                &exc_info))
        return NULL;

    if (exc_info != NULL && exc_info != Py_None) {
        /* If the headers have already been sent, just propagate the
           exception. */
        if (self->headers_sent) {
            PyObject *type, *value, *tb;
            if (!PyArg_ParseTuple(exc_info, "OOO", &type, &value, &tb))
                return NULL;
            Py_INCREF(type);
            Py_INCREF(value);
            Py_INCREF(tb);
            PyErr_Restore(type, value, tb);
            return NULL;
        }
    }
    else if (data->status != 0 || data->headers != NULL) {
        data->err = "headers already set";
        return NULL;
    }

    /* TODO validation of status and headers */
    if (!PyArg_ParseTuple(status, "i", &data->status))
        return NULL;

    PyObject *iterator = PyObject_GetIter(headers);
    PyObject *item;

    if (iterator == NULL) {
        data->err = "headers is NULL";
        return NULL;
    }

    while (item = PyIter_Next(iterator)) {
        char *field, *value;
        int ret;
        if (!PyArg_ParseTuple(status, "(s,s)", &field, &value))
            return NULL;
        if (!strcasecmp(field, "content-length"))
            data->response_length = atoi(field);
        else if (!strcasecmp(field, "connection")){
            if (!strcasecmp(value, "upgrade"))
                data->upgrade = 1;
        }
        ret = dictAdd(data->headers, wstrDup(field), wstrDup(value));
        Py_DECREF(item);
        if (ret == DICT_WRONG) {
            Py_DECREF(iterator);
            return NULL;
        }
    }

    Py_DECREF(iterator);

    if (PyErr_Occurred()) {
        return NULL;
    }

    return PyObject_GetAttrString((PyObject *)self, "write");
}

/* Send a chunk of data */
static int wsgiSendBody(struct response *response, const char *data, int len)
{
    struct wsgiData *wsgi_data = response->client->app_private_data;
    struct httpData *http_data = response->client->protocol_data;
    int tosend = len, restsend;
    if (!len)
        return 0;
    if (wsgi_data->response_length != 0) {
        if (wsgi_data->send > wsgi_data->response_length)
            return 0;
        restsend = wsgi_data->response_length - wsgi_data->send;
        tosend = restsend > tosend ? tosend: restsend;
    }
    if (is_chunked(wsgi_data->response_length, http_data->protocol_version, wsgi_data->status) && tosend == 0)
        return 0;

    wsgi_data->send += tosend;
    response->client->res_buf = wstrCatLen(response->client->res_buf, data, tosend);
    if (response->client->res_buf == NULL)
        return -1;

    return 0;
}

/* write() callable implementation */
static PyObject *responseWrite(struct response *self, PyObject *args)
{
    struct wsgiData *wsgi_data = self->client->app_private_data;
    const char *data;
    int dataLen;

    if (wsgi_data->status == 0 && wsgi_data->headers == NULL) {
        wsgi_data->err = "write() before start_response()";
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "s#:write", &data, &dataLen))
        return NULL;

    /* Send headers if necessary */
    if (!self->headers_sent) {
        if (wsgiSendHeaders(self))
            return NULL;
    }

    if (wsgiSendBody(self, data, dataLen))
        return NULL;

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

void sendResponse500(struct response *response)
{
    struct wsgiData *wsgi_data = response->client->app_private_data;
    static const char *headers[] = {
        "Content-Type", "text/html; charset=iso-8859-1",
    };
    static const char *body =
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
        "<html><head>\n"
        "<title>500 Internal Error</title>\n"
        "</head><body>\n"
        "<h1>Internal Error</h1>\n"
        "<p>The server encountered an unexpected condition which\n"
        "prevented it from fulfilling the request.</p>\n"
        "</body></html>\n";
    wsgi_data->status = 500;

    if (!wsgiSendHeaders(response))
        wsgiSendBody(response, (uint8_t *)body, strlen(body));
}

