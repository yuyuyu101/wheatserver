CC=cc
CXX=c++
SOURCES = config.c net.c log.c wstr.c list.c dict.c hook.c sig.c networking.c \
		  worker_process.c worker_sync.c worker_async.c util.c protocol.c \
		  application.c http_parser.c app_wsgi.c wsgiinput.c wsgiwrapper.c \
		  stats.c event.c setproctitle.c proto_http.c app_static_file.c
OBJECTS = $(SOURCES:.c=.o)
SERVER_OBJECTS += $(OBJECTS)
SERVER_OBJECTS += wheatserver.o
WORKER_OBJECTS += $(OBJECTS)
WORKER_OBJECTS += wheatworker.o
PYTHON_VERSION = $(shell python -c "import distutils.sysconfig;print distutils.sysconfig.get_python_version()")
PYTHON_LIBS = ./
LIBS += -L$(PYTHON_LIBS) -lpython$(PYTHON_VERSION)
PYTHON_FLAGS = -I$(shell python -c "import distutils.sysconfig;print distutils.sysconfig.get_python_inc()")
EXTRA ?=  
CFLAGS = -O0 -Wall -g $(EXTRA)

TESTS = test_wstr test_list test_dict

all: wheatserver wheatworker

wheatworker.o: wheatserver.c wheatserver.h
	$(CC) $(CFLAGS) -c $< -o $@ -DWHEAT_DEBUG_WORKER

%wsgi.o: %wsgi.c
	$(CC) $(CFLAGS) $(PYTHON_FLAGS) -c $< -o $@
wsgi%.o: wsgi%.c
	$(CC) $(CFLAGS) $(PYTHON_FLAGS) -c $< -o $@

%.o: %.c
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

.PHONY: clean
clean:
	rm $(SERVER_OBJECTS) *.gch wheatserver wheatworker
