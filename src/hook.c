// Used by modules to hook actions to master process
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "hook.h"
#include "wheatserver.h"

void initHookCenter()
{
    Server.hook_center = wmalloc(sizeof(struct hookCenter));
    if (Server.hook_center == NULL) {
        wheatLog(WHEAT_WARNING, "init hookcenter failed");
        halt(1);
        return ;
    }

    Server.hook_center->afterinit = createList();
    Server.hook_center->whenready = createList();
    Server.hook_center->prefork_worker = createList();
    Server.hook_center->afterfork_worker = createList();
    Server.hook_center->whenwake = createList();
    Server.hook_center->beforesleep = createList();
}
