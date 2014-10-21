import socket

from wheatserver_test import WheatServer, PROJECT_PATH, server_socket
import memcache

STORED = "STORED\r\n"
GET_END = "END\r\n"
NOT_STORED = "NOT_STORED\r\n"
DELETED = "DELETED\r\n"
EXISTS = "EXISTS\r\n"
NOT_FOUND = "NOT_FOUND\r\n"

def test_memcache_basic():
    mc = memcache.Client(['127.0.0.1:10828'], debug=0)
    for i in range(100):
        assert(mc.set("key_%s" % i, "value_%s" % i) == True)

    for i in range(100):
        assert(mc.get("key_%s" % i) == "value_%s" % i)

    for i in range(100):
        assert(mc.delete("key_%s" % i) == 1)

    for i in range(100):
        assert(mc.get("key_%s" % i) is None)



class MemcacheClient(object):
    def __init__(self, host, port, timeout=3):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.buf = ""
        if hasattr(self.socket, 'settimeout'):
            self.socket.settimeout(timeout)

        try:
            self.socket.connect((host, port))
        except socket.error as msg:
            print msg
            assert(0)


    def send_cmd(self, cmd):
        self.socket.sendall(cmd)

    def expect_result(self, r):
        buf = ""
        if len(self.buf) > len(r):
            buf = self.buf[:len(r)]
            self.buf = self.buf[len(r):]
        else:
            left = len(r) - len(self.buf)
            buf = self.buf
            self.buf = ""
            try:
                buf += self.socket.recv(left)
            except socket.error as msg:
                print msg
                assert(0)
        try:
            assert(buf == r)
        except Exception:
            print buf
            print r
            assert(0)

    def get_version(self, k):
        self.send_cmd("gets %s\r\n" % k)
        self.buf += self.socket.recv(4096)
        index = 0
        while True:
            index = self.buf.find('\r\n')
            if index >= 0:
                break
            self.buf += self.socket.recv(4096)
        line = self.buf[:index]
        value, key, flags, bytes, cas = line.split()
        self.buf = self.buf[index+2:]
        while True:
            index = self.buf.find('END\r\n')
            if index >= 0:
                self.buf = self.buf[index+5:]
                break
            self.buf += self.socket.recv(4096)
        return cas


def test_memcache_basic_response():
    c = MemcacheClient('127.0.0.1', 10828)
    key = "key"
    value = "value"
    flag = 1
    expire = 0

    c.send_cmd("set %s %s %s %s \r\n%s\r\n" % (key, flag, expire, len(value),
                                               value))
    c.expect_result(STORED)

    value = "value 1"
    c.send_cmd("set %s %s %s %s noreply\r\n%s\r\n" % (key, flag, expire,
                                                      len(value), value))
    c.send_cmd("get %s\r\n" % key)
    c.expect_result("VALUE %s %s %s\r\n%s\r\n%s" % (key, flag, len(value), value,
                                                    GET_END))

    c.send_cmd("add %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(value),
                                              value))
    c.expect_result(NOT_STORED)
    c.send_cmd("delete %s\r\n" % key)
    c.expect_result(DELETED)
    value = "value 2"
    c.send_cmd("add %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(value),
                                              value))
    c.expect_result(STORED)
    version = c.get_version(key)
    value = "value 3"
    c.send_cmd("cas %s %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(value),
                                                 version, value))
    c.expect_result(STORED)
    c.send_cmd("get %s\r\n" % key)
    c.expect_result("VALUE %s %s %s\r\n%s\r\n%s" % (key, flag,
                                                    len(value), value,
                                                    GET_END))

    c.send_cmd("cas %s %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(value),
                                                 version, value))
    c.expect_result(EXISTS)

    c.send_cmd("delete %s\r\n" % key)
    c.expect_result(DELETED)
    c.send_cmd("cas %s %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(value),
                                                 version, value))
    c.expect_result(NOT_FOUND)

    c.send_cmd("replace %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(value),
                                                  value))
    c.expect_result(NOT_STORED)
    c.send_cmd("add %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(value),
                                              value))
    c.expect_result(STORED)
    value = "value 4"
    c.send_cmd("replace %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(value),
                                                  value))
    c.expect_result(STORED)

    avalue = "value 5"
    flag = 2
    c.send_cmd("append %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(avalue),
                                                 avalue))
    c.expect_result(STORED)

    c.send_cmd("get %s\r\n" % key)
    c.expect_result("VALUE %s %s %s\r\n%s\r\n%s" % (key, flag, len(value+avalue), value+avalue,
                                                    GET_END))

    c.send_cmd("prepend %s %s %s %s\r\n%s\r\n" % (key, flag, expire, len(avalue),
                                                  avalue))
    c.expect_result(STORED)

    c.send_cmd("get %s\r\n" % key)
    c.expect_result("VALUE %s %s %s\r\n%s\r\n%s" % (key, flag,
                                                    len(value+avalue+avalue),
                                                    avalue+value+avalue, GET_END))

    c.send_cmd("delete %s\r\n" % key)
    c.expect_result(DELETED)

    c.send_cmd("delete %s\r\n" % key)
    c.expect_result(NOT_FOUND)

    value = 100
    c.send_cmd("set %s %s %s %s noreply\r\n%s\r\n" % (key, flag, expire,
                                                      len(str(value)), value))
    c.send_cmd("incr %s %s noreply\r\n" % (key, 2))
    c.send_cmd("decr %s %s\r\n" % (key, 1))
    c.expect_result("%s\r\n" % 101)

    c.send_cmd("delete %s\r\n" % key)
    c.expect_result(DELETED)
