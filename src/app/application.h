// Application common utils
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_APPLICANT_H
#define WHEATSERVER_APPLICANT_H

#include "../worker/worker.h"
#include "../wheatserver.h"

int initAppData(struct conn *);
int initApp(struct app *);
void getAppsByProtocol(struct array *apps, char *protocol_name);

#endif
