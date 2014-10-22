import collections
import random
import socket
import time

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
    def __init__(self):
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
        if not self.data:
             return
        k = random.choice(self.data.keys())
        assert(str(self.data[k]) == str(self.c.get(k)))

    def set(self):
        k = self.get_str(random.randint(10, 100))
        if k not in self.data:
            if len(k) % 3 == 0:
                v = random.randint(0, 1000000)
                self.num_key.add(k)
            else:
                v = self.get_str(random.randint(100, 4096))
                if k in self.num_key:
                    self.num_key.remove(k)

        assert(self.c.set(k, v))
        self.data[k] = str(v)

    def delete(self):
        if not self.data:
            return
        k = random.choice(self.data.keys())
        assert(self.c.delete(k))
        if k in self.num_key:
            self.num_key.remove(k)
        del self.data[k]

    def append(self):
        if not self.data:
            return

        k = random.choice(self.data.keys())
        v = self.get_str(random.randint(100, 1024))
        assert(self.c.append(k, v))
        self.data[k] += str(v)
        if k in self.num_key:
            self.num_key.remove(k)

    def prepend(self):
        if not self.data:
            return

        k = random.choice(self.data.keys())
        v = self.get_str(random.randint(100, 1024))
        assert(self.c.prepend(k, v))
        self.data[k] = str(v) + str(self.data[k])
        if k in self.num_key:
            self.num_key.remove(k)

    def incr(self):
        if not self.num_key:
            return
        k = random.sample(self.num_key, 1)[0]
        v = random.randint(0, 1000000)
        self.data[k] = str(int(self.data[k]) + v)
        assert(str(self.c.incr(k, v)) == self.data[k])

    def decr(self):
        if not self.num_key:
            return
        k = random.sample(self.num_key, 1)[0]
        v = random.randint(0, 1000000)
        self.data[k] = str(int(self.data[k]) - v)
        if int(self.data[k]) < 0:
            self.data[k] = "0"
        assert(str(self.c.decr(k, v)) == self.data[k])

    def add(self):
        while True:
            k = self.get_str(random.randint(10, 100))
            if k not in self.data:
                if len(k) % 3 == 0:
                    v = random.randint(0, 1000000)
                    self.num_key.add(k)
                else:
                    v = self.get_str(random.randint(100, 4096))
                break

        assert(self.c.add(k, v))
        self.data[k] = str(v)
        if k in self.num_key:
            self.num_key.remove(k)

    def replace(self):
        if not self.data:
            return
        k = random.choice(self.data.keys())
        v = self.get_str(random.randint(100, 4096))
        assert(self.c.replace(k, v))
        self.data[k] = str(v)
        if k in self.num_key:
            self.num_key.remove(k)

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
            elif n > 60:
                self.prepend()
            elif n > 55:
                self.append()
            elif n > 45:
                self.delete()
            elif n > 30:
                self.set()
            else:
                self.get()
            num -= 1


def test_memcache_synthetic():
    s = MemcacheSynthetic()
    random.seed(time.time())
    s.run(10000)
