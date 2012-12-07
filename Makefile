WORKER_OBJECTS = wheatworker.o config.o net.o log.o wstr.o list.o dict.o hook.o sig.o networking.o worker_process.o worker_sync.o util.o protocol.o application.o http_parser.o
SERVER_OBJECTS = wheatserver.o config.o net.o log.o wstr.o list.o dict.o hook.o sig.o networking.o worker_process.o worker_sync.o util.o protocol.o application.o http_parser.o

all: wheatserver testso wheatworker

wheatserver: $(SERVER_OBJECTS)
	cc -o wheatserver $(SERVER_OBJECTS) -g
wheatserver.o: wheatserver.c wheatserver.h
	cc -c wheatserver.c wheatserver.h -g
config.o: config.c wheatserver.h
	cc -c config.c wheatserver.h -g
net.o: net.c net.h
log.o: log.c wheatserver.h
wstr.o: wstr.c wstr.h
	cc -c wstr.c wstr.h -g
list.o: list.c list.h
	cc -c list.c list.h -g
dict.o: dict.c dict.h
	cc -c dict.c dict.h -g
hook.o: hook.c hook.h
	cc -c hook.c hook.h -g
sig.o: sig.c sig.h
	cc -c sig.c sig.h -g
networking.o: networking.c networking.h
	cc -c networking.c networking.h -g
worker_process.o: worker_process.c worker_process.h
	cc -c worker_process.c worker_process.h -g
worker_sync.o: worker_sync.c
	cc -c wheatserver.h worker_sync.c -g
util.o: util.c util.h
	cc -c util.c util.h -g
protocol.o: protocol.c wheatserver.h
	cc -c protocol.c wheatserver.h -g
application.o: application.c wheatserver.h
	cc -c application.c wheatserver.h


http_parser.o: http_parser.c http_parser.h
	cc -c http_parser.c http_parser.h -g

################

wheatworker: $(WORKER_OBJECTS)
	cc -o wheatworker $(WORKER_OBJECTS) -g

wheatworker.o: wheatserver.c wheatserver.h
	cc -o wheatworker.o -c wheatserver.c -g -DWHEAT_DEBUG_WORKER


###########
test: test_wstr test_list

test_wstr: wstr.c wstr.h
	cc -o test_wstr wstr.c -DWSTR_TEST_MAIN
	./test_wstr

test_list: list.c list.h
	cc -o test_list list.c -DLIST_TEST_MAIN
	./test_list

test_dict: dict.c dict.h
	cc -o test_dict dict.c wstr.o -DDICT_TEST_MAIN
	./test_dict

testso: $(SERVER_OBJECTS)
	cc $(SERVER_OBJECTS) -fPIC -shared -o tests/tests.so

clean:
	rm $(SERVER_OBJECTS) wheatserver a.out
