#include "wheatserver.h"
#include "application.h"

struct app appTable[] = {
    {"wsgi", wsgiConstructor, initWsgi, deallocWsgi, initWsgiAppData, freeWsgiAppData}
};

struct app *spotAppInterface()
{
    static int is_init = 0;
    if (!is_init) {
        appTable[0].initApp();
        is_init = 1;
    }
    return &appTable[0];
}


/* ========== wsgi callback ========== */
static PyObject *pApp = NULL;

int wsgiConstructor(struct client *client)
{
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
    /* Create Request object, passing it the context as a CObject */
    PyObject *res = PyCObject_FromVoidPtr(client, NULL);
    PyObject *start_resp, *result, *args, *env;
    struct response *req_obj;
    if (res == NULL)
        goto out;

    env = create_environ(client);
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
        if (req_obj == NULL || !req_obj->headers_sent)
            sendResponse500((struct response *)req_obj);
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
    data->status_msg = wstrEmpty();
    data->send = 0;
    data->response_length = 0;
    data->headers = dictCreate(&wstrDictType);
    data->err = NULL;

    return data;
}

void freeWsgiAppData(void *data)
{
    struct wsgiData *d = data;
    dictRelease(d->headers);
    free(d);
}

void initWsgi()
{
    char *app_t;
    char buf[WHEATSERVER_PATH_LEN];
    struct configuration *conf;
    Py_Initialize();

    conf = getConfiguration("app-module-path");
    snprintf(buf, WHEATSERVER_PATH_LEN, "import sys, os\n"
            "sys.path.append(os.getcwd())\nsys.path.append('%s')", conf->target.ptr);
    PyRun_SimpleString(buf);

    init_wsgisup();
    conf = getConfiguration("app-module-name");
    app_t = conf->target.ptr;

    PyObject *pModule, *pName = PyString_FromString(app_t);
    if (pName == NULL)
        goto err;

    pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    if (pModule == NULL)
        goto err;

    conf = getConfiguration("app-name");
    app_t = conf->target.ptr;
    pApp = PyObject_GetAttrString(pModule, app_t);
    Py_DECREF(pModule);
    if (pApp == NULL || !PyCallable_Check(pApp)) {
        Py_XDECREF(pApp);
        goto err;
    }

    return ;
err:
    PyErr_Print();
    wheatLog(WHEAT_WARNING, "initWsgi failed");
    halt(1);
}

void deallocWsgi()
{
    Py_DECREF(pApp);
    Py_Finalize();
}
