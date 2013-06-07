CC=cc

CORE_SOURCES = config.c net.c log.c wstr.c list.c dict.c hook.c sig.c \
			   networking.c util.c register.c stats.c event.c setproctitle.c \
			   slice.c debug.c portable.c memalloc.c array.c \
			   app/application.c protocol/protocol.c worker/mbuf.c \
			   worker/worker.c modules.c
include Module.mk

SOURCES += $(CORE_SOURCES)
SOURCES += $(MODULE_SOURCES)

OBJECTS = $(SOURCES:.c=.o)
SERVER_OBJECTS += $(OBJECTS)
SERVER_OBJECTS += wheatserver.o
WORKER_OBJECTS += $(OBJECTS)
WORKER_OBJECTS += wheatworker.o
DEBUG ?= 0
ifeq ($(DEBUG),1)
CFLAGS += -O0 -Wall -g $(EXTRA)
else
CFLAGS += -O3 -Wall $(EXTRA)
endif

TESTS = test_wstr test_list test_dict test_slice test_mbuf test_array

all: build_module_table wheatserver wheatworker

wheatworker.o: wheatserver.c wheatserver.h
	$(CC) $(CFLAGS) -c $< -o $@ -DWHEAT_DEBUG_WORKER

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@
################
wheatserver: $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) -o wheatserver $(SERVER_OBJECTS) $(LIBS)

wheatworker: $(WORKER_OBJECTS)
	$(CC) $(CFLAGS) -o wheatworker $(WORKER_OBJECTS) $(LIBS)

build_module_table:
	python build_module_table.py "$(MODULE_ATTRS)"

###########
test: $(TESTS)
	for t in $(TESTS); do echo "***** Running $$t"; ./$$t ; rm $$t || exit 1; done

test_wstr: wstr.c wstr.h
	$(CC) -o $@ wstr.c memalloc.o -DWSTR_TEST_MAIN
	./test_wstr

test_list: list.c list.h
	$(CC) -o $@ list.c memalloc.o -DLIST_TEST_MAIN
	./test_list

test_dict: dict.c dict.h
	$(CC) -o $@ dict.c wstr.c memalloc.o -DDICT_TEST_MAIN
	./test_dict

test_slice: slice.c slice.h
	$(CC) -o $@ slice.c memalloc.c -DSLICE_TEST_MAIN
	./test_slice

test_array: array.c array.h
	$(CC) -o $@ array.c memalloc.c -DARRAY_TEST_MAIN
	./test_array

test_mbuf: worker/mbuf.c worker/mbuf.h slice.c slice.h
	$(CC) -o $@ worker/mbuf.c slice.c memalloc.c -DMBUF_TEST_MAIN
	./test_mbuf

.PHONY: clean
clean:
	rm $(SERVER_OBJECTS) *.gch wheatserver wheatworker wheatworker.o
