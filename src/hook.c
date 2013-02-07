#include "hook.h"
#include "wheatserver.h"

void initHookCenter()
{
    Server.hook_center = malloc(sizeof(struct hookCenter));
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
