import collections
import random
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

    c.send_cmd("decr %s %s\r\n" % (key, 1))
    c.expect_result("CLIENT_ERROR INVALID ARGUMENT\r\n")

    c.send_cmd("delete %s\r\n" % key)
    c.expect_result(DELETED)

    c.send_cmd("delete %s\r\n" % key)
    c.expect_result(NOT_FOUND)

    c.send_cmd("decr %s %s\r\n" % (key, 1))
    c.expect_result(NOT_FOUND)

    value = 100
    c.send_cmd("set %s %s %s %s noreply\r\n%s\r\n" % (key, flag, expire,
                                                      len(str(value)), value))
    c.send_cmd("incr %s %s noreply\r\n" % (key, 2))
    c.send_cmd("decr %s %s\r\n" % (key, 1))
    c.expect_result("%s\r\n" % 101)
    c.send_cmd("decr %s %s\r\n" % (key, 102))
    c.expect_result("%s\r\n" % 0)

    c.send_cmd("delete %s\r\n" % key)
    c.expect_result(DELETED)


class MemcacheSynthetic(object):
    def __ini__(self):
        self.data = collections.defaultdict(str)
        self.c = memcache.Client(["127.0.0.1:10828"])
        self.num_key = set()

    def get_str(self, n):
        s = ""
        while n:
            s += random.choice("abcdefghijklmnopqrstuvwxyz1234567890")
            n -= 1
        return s

    def get(self):
        k = ramdom.choice(self.data.keys())
        assert(self.data[k] == self.c.get(k))

    def set(self):
        k = self.get_str(random.randint(10, 100))
        if k not in self.data:
            if len(k) % 3 == 0:
                v = random.randint(0, 1000000)
                self.num_key.insert(k)
            else:
                v = self.get_str(random.randint(100, 4096))
            break

        self.c.set(k, v)
        self.data[k] = v

    def append(self):
        if random.randint(0, 1):
            k = ramdom.choice(self.data.keys())
        else:
            k = self.get_str(random.randint(10, 100))

        v = self.get_str(random.randint(100, 1024))
        self.c.append(k, v)
        self.data[k] += v
        if k in self.num_key:
            self.num_key.remove(k)

    def prepend(self):
        if random.randint(0, 1):
            k = ramdom.choice(self.data.keys())
        else:
            k = self.get_str(random.randint(10, 100))

        v = self.get_str(random.randint(100, 1024))

        self.c.prepend(k, v)
        self.data[k] = v + self.data[k]
        if k in self.num_key:
            self.num_key.remove(k)

    def incr(self):
        if not self.num_key:
            return
        k = ramdom.choice(self.num_key)
        v = random.randint(0, 1000000)
        self.c.incr(k, n)
        self.data[k] = int(self.data[k]) + n

    def decr(self):
        if not self.num_key:
            return
        k = ramdom.choice(self.num_key)
        v = random.randint(0, 1000000)
        self.c.decr(k, n)
        self.data[k] = int(self.data[k]) - n
        if self.data[k] < 0:
            self.data[k] = 0

    def add(self, k, v):
        while True:
            k = self.get_str(random.randint(10, 100))
            if k not in self.data:
                if len(k) % 3 == 0:
                    v = random.randint(0, 1000000)
                    self.num_key.insert(k)
                else:
                    v = self.get_str(random.randint(100, 4096))
                break

        self.c.add(k, v)
        self.data[k] = v

    def replace(self):
        if not self.data:
            return
        k = ramdom.choice(self.data.keys())
        v = self.get_str(random.randint(100, 4096))
        self.c.replace(k, v)
        self.data[k] = v

    def run(self, num):
        while num:
            n = random.randint(0, 100)
            if n > 90:
                self.replace()
            elif n > 80:
                self.add()
            elif n > 70:
                self.incr()
            elif n > 65:
                self.decr()
            elif n > 55:
                self.prepend()
            elif n > 50:
                self.append()
            elif n > 30:
                self.set()
            else:
                self.get()


def test_memcache_synthetic():
    s = MemcacheSynthetic()
    s.run(10000)
