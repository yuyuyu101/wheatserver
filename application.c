#include "wheatserver.h"
#include "application.h"

struct app appTable[] = {
    {"wsgi", wsgiConstructor, NULL, initWsgiAppData, freeWsgiAppData}
};

struct app *spotAppInterface()
{
    return &appTable[0];
}


/* ========== wsgi callback ========== */

int wsgiConstructor(struct client *client)
{
    struct wsgiData *wsgi_data = client->app_private_data;
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
    PyObject *env = create_environ(client);
    /* Create Request object, passing it the context as a CObject */
    PyObject *res = PyCObject_FromVoidPtr(client, NULL);
    PyObject *start_resp, *result, *args;
    struct response *req_obj;
    if (res == NULL)
        goto out;

    args = Py_BuildValue("(O)", res);
    Py_DECREF(res);
    if (args == NULL)
        goto out;

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

    result = PyObject_CallObject(wsgi_data->pApp, args);
    Py_DECREF(args);
    if (result != NULL) {
        /* Handle the application response */
        char *r;
        if (!PyArg_ParseTuple(result, "s", &r))
            return -1;
        client->res_buf = wstrCat(client->res_buf, r);
        /* result now owned by req_obj */
        wsgiCallClose(result);
    }

out:
    if (PyErr_Occurred()) {
        PyErr_Print();

        /* Display HTTP 500 error, if possible */
        if (req_obj == NULL || !req_obj->headers_sent)
            sendResponse500((struct response *)res);
    }

    if (req_obj != NULL) {
        /* Don't rely on cyclic GC. Clear circular references NOW. */
        responseClear(req_obj);

        Py_DECREF(req_obj);
    }

    PyGILState_Release(gstate);
    return 0;
}

void *initWsgiAppData()
{
    struct wsgiData *data = malloc(sizeof(struct wsgiData));
    if (data == NULL)
        return NULL;
    data->environ = NULL;
    data->response = NULL;
    data->status = 0;
    data->send = 0;
    data->response_length = 0;
    data->headers = NULL;
    data->err = NULL;

    PyObject *pModule, *pName = PyString_FromString("sample");
    if (pName == NULL)
        goto err;

    pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    if (pModule == NULL)
        goto err;

    data->pApp = PyObject_GetAttrString(pModule, (char *)"sample");
    Py_DECREF(pModule);
    if (data->pApp == NULL || !PyCallable_Check(data->pApp)) {
        Py_XDECREF(data->pApp);
        goto err;
    }

    return data;
err:
    PyErr_Print();
    return NULL;
}

void freeWsgiAppData(void *data)
{
    struct wsgiData *d = data;
    dictRelease(d->headers);
    free(d);
}
