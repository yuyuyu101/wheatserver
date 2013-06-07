#include "wheatserver.h"
extern struct moduleAttr AppWsgiAttr;
extern struct moduleAttr AppStaticAttr;
extern struct moduleAttr AppRedisAttr;
extern struct moduleAttr ProtocolHttpAttr;
extern struct moduleAttr ProtocolRedisAttr;
extern struct moduleAttr SyncWorkerAttr;
extern struct moduleAttr AsyncWorkerAttr;
struct moduleAttr *ModuleTable[] = {
&AppWsgiAttr,
&AppStaticAttr,
&AppRedisAttr,
&ProtocolHttpAttr,
&ProtocolRedisAttr,
&SyncWorkerAttr,
&AsyncWorkerAttr,
NULL};
