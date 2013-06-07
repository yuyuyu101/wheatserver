WSGI_APP_MODULE = app/wsgi/app_wsgi.c app/wsgi/wsgiwrapper.c app/wsgi/wsgiinput.c
PYTHON_VERSION = $(shell python -c "import distutils.sysconfig;print distutils.sysconfig.get_python_version()")
MODULE_ATTRS += AppWsgiAttr

CFLAGS += -I$(shell python -c "import distutils.sysconfig;print distutils.sysconfig.get_python_inc()")
LIBS += -lpython$(PYTHON_VERSION)
MODULE_SOURCES += $(WSGI_APP_MODULE)

################################ Module Separtor ###############################
STATIC_APP_MODULE = app/static/app_static_file.c

MODULE_SOURCES += $(STATIC_APP_MODULE)
MODULE_ATTRS += AppStaticAttr

################################ Module Separtor ###############################
REDIS_APP_MODULE = app/wheatredis/redis.c app/wheatredis/hashkit.c \
				   app/wheatredis/md5.c app/wheatredis/redis_config.c

MODULE_SOURCES += $(REDIS_APP_MODULE)
MODULE_ATTRS += AppRedisAttr

################################ Module Separtor ###############################
HTTP_PROTOCOL_MODULE = protocol/http/http_parser.c protocol/http/proto_http.c

MODULE_SOURCES += $(HTTP_PROTOCOL_MODULE)
MODULE_ATTRS += ProtocolHttpAttr

################################ Module Separtor ###############################
REDIS_PROTOCOL_MODULE = protocol/redis/proto_redis.c

MODULE_SOURCES += $(REDIS_PROTOCOL_MODULE)
MODULE_ATTRS += ProtocolRedisAttr

################################ Module Separtor ###############################
SYNC_WORKER_MODULE = worker/worker_sync.c

MODULE_SOURCES += $(SYNC_WORKER_MODULE)
MODULE_ATTRS += SyncWorkerAttr

################################ Module Separtor ###############################
ASYNC_WORKER_MODULE = worker/worker_async.c

MODULE_SOURCES += $(ASYNC_WORKER_MODULE)
MODULE_ATTRS += AsyncWorkerAttr
