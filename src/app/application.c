// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "application.h"

int initApp(struct app *app)
{
    if (app->is_init)
        return WHEAT_OK;
    if (app->initApp(WorkerProcess->protocol) == WHEAT_WRONG)
        return WHEAT_WRONG;
    app->is_init = 1;
    return WHEAT_OK;
}

int initAppData(struct conn *c)
{
    if (c->app && c->app->initAppData) {
        c->app_private_data = c->app->initAppData(c);
        if (!c->app_private_data)
            return WHEAT_WRONG;
    }
    return WHEAT_OK;
}

void getAppsByProtocol(struct array *apps, char *protocol_name)
{
    struct app **app;

    app = &AppTable[0];
    while (*app) {
        if (!strcasecmp(protocol_name, (*app)->proto_belong))
            arrayPush(apps, app);
        app++;
    }
}
