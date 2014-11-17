#include "wheatserver.h"
extern struct moduleAttr AppWsgiAttr;
extern struct moduleAttr AppStaticAttr;
extern struct moduleAttr AppRedisAttr;
extern struct moduleAttr AppRamcloudAttr;
extern struct moduleAttr ProtocolHttpAttr;
extern struct moduleAttr ProtocolMemcacheAttr;
extern struct moduleAttr ProtocolRedisAttr;
extern struct moduleAttr SyncWorkerAttr;
extern struct moduleAttr AsyncWorkerAttr;
struct moduleAttr *ModuleTable[] = {
&AppWsgiAttr,
&AppStaticAttr,
&AppRedisAttr,
&AppRamcloudAttr,
&ProtocolHttpAttr,
&ProtocolMemcacheAttr,
&ProtocolRedisAttr,
&SyncWorkerAttr,
&AsyncWorkerAttr,
NULL};
