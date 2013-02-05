CC=cc
CXX=c++
SOURCES = config.c net.c log.c wstr.c list.c dict.c hook.c sig.c networking.c worker_process.c worker_sync.c util.c protocol.c application.c http_parser.c wsgi.c wsgiinput.c wsgiwrapper.c stats.c event.c
OBJECTS = $(SOURCES:.c=.o)
SERVER_OBJECTS += $(OBJECTS)
SERVER_OBJECTS += wheatserver.o
WORKER_OBJECTS += $(OBJECTS)
WORKER_OBJECTS += wheatworker.o
LIBS = -L/System/Library/Frameworks/Python.framework/Versions/2.7/lib/python2.7/config -lpython2.7
CFLAGS = -O0 -Wall -g

TESTS = test_wstr test_list test_dict

all: wheatserver wheatworker

wheatworker.o: wheatserver.c wheatserver.h
	$(CC) $(CFLAGS) -c $< -o $@ -DWHEAT_DEBUG_WORKER

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@
################

wheatserver: $(SERVER_OBJECTS)
	$(CC) -o wheatserver $(SERVER_OBJECTS) $(LIBS)

wheatworker: $(WORKER_OBJECTS)
	$(CC) -o wheatworker $(WORKER_OBJECTS) $(LIBS)

###########
test: $(TESTS)
	for t in $(TESTS); do echo "***** Running $$t"; ./$$t ; rm $(TEST) || exit 1; done

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
