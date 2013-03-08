CC=cc
CXX=c++

APP_SOURCES = app/application.c \
			  app/wsgi/app_wsgi.c app/wsgi/wsgiwrapper.c app/wsgi/wsgiinput.c \
			  app/static/app_static_file.c 
PROTO_SOURCES =  protocol/protocol.c \
				 protocol/http/http_parser.c protocol/http/proto_http.c
WORKER_SOURCES = worker/worker_async.c worker/worker_sync.c worker/worker.c \
				 worker/mbuf.c
SOURCES = config.c net.c log.c wstr.c list.c dict.c hook.c sig.c networking.c \
		  util.c register.c stats.c event.c setproctitle.c slice.c debug.c \
		  portable.c
SOURCES += $(APP_SOURCES)
SOURCES += $(PROTO_SOURCES)
SOURCES += $(WORKER_SOURCES)

OBJECTS = $(SOURCES:.c=.o)
SERVER_OBJECTS += $(OBJECTS)
SERVER_OBJECTS += wheatserver.o
WORKER_OBJECTS += $(OBJECTS)
WORKER_OBJECTS += wheatworker.o
PYTHON_VERSION = $(shell python -c "import distutils.sysconfig;print distutils.sysconfig.get_python_version()")
LIBS += -lpython$(PYTHON_VERSION)
PYTHON_FLAGS = -I$(shell python -c "import distutils.sysconfig;print distutils.sysconfig.get_python_inc()")
EXTRA ?=  
CFLAGS = -O0 -Wall -g $(EXTRA)

TESTS = test_wstr test_list test_dict test_slice test_mbuf

all: wheatserver wheatworker

wheatworker.o: wheatserver.c wheatserver.h
	$(CC) $(CFLAGS) -c $< -o $@ -DWHEAT_DEBUG_WORKER

%wsgi.o: %wsgi.c
	$(CC) $(CFLAGS) $(PYTHON_FLAGS) -c $< -o $@
wsgi%.o: wsgi%.c
	$(CC) $(CFLAGS) $(PYTHON_FLAGS) -c $< -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@
################

wheatserver: $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) -o wheatserver $(SERVER_OBJECTS) $(LIBS)

wheatworker: $(WORKER_OBJECTS)
	$(CC) $(CFLAGS) -o wheatworker $(WORKER_OBJECTS) $(LIBS)

###########
test: $(TESTS)
	for t in $(TESTS); do echo "***** Running $$t"; ./$$t ; rm $$t || exit 1; done

test_wstr: wstr.c wstr.h
	$(CC) -o $@ wstr.c -DWSTR_TEST_MAIN
	./test_wstr

test_list: list.c list.h
	$(CC) -o $@ list.c -DLIST_TEST_MAIN
	./test_list

test_dict: dict.c dict.h
	$(CC) -o $@ dict.c wstr.o -DDICT_TEST_MAIN
	./test_dict

test_slice: slice.c slice.h
	$(CC) -o $@ slice.c -DSLICE_TEST_MAIN
	./test_slice

test_mbuf: worker/mbuf.c worker/mbuf.h slice.c slice.h
	$(CC) -o $@ worker/mbuf.c slice.c -DMBUF_TEST_MAIN
	./test_mbuf

.PHONY: clean
clean:
	rm $(SERVER_OBJECTS) *.gch wheatserver wheatworker

