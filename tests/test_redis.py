from wheatserver_test import WheatServer, PROJECT_PATH, server_socket
import time
import os
import redis
import signal

class RedisServer(object):
    def __init__(self, *options):
        self.exec_pid = os.fork()
        if not self.exec_pid:
            os.execlp("redis-server", *options)

    def __del__(self):
        os.kill(self.exec_pid, signal.SIGQUIT);


def test_redis():
    async = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % PROJECT_PATH + "/example/",
                               "--static-file-dir %s" % "/static",
                               "--protocol Redis")
    time.sleep(0.1)
    r = redis.StrictRedis(port=10828)
    for i in range(100):
        time.sleep(0.001)
        assert r.set(str(i), i)
    for i in range(100):
        time.sleep(0.001)
        assert r.get(str(i)) == str(i)

    assert r.get('z') == None

def test_redis_conf():
    redis1 = RedisServer("", "--port 18000")
    redis2 = RedisServer("", "--port 18001")
    async = WheatServer("redis.conf", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % PROJECT_PATH + "/example/",
                               "--static-file-dir %s" % "/static",
                               "--protocol Redis",
                               "--config-source RedisThenFile",
                               "--config-server 127.0.0.1:18000",
                               "--logfile test_redis_conf.log"
                               )
    time.sleep(0.1)
    r = redis.StrictRedis(port=10828)
    assert r.set(1, 1)
    assert r.get(1) == '1'
    f = open("test_redis_conf.log")
    content = f.read(10000)
    f.close()
    assert "get config from redis failed" in content
    assert "get config from file successful" in content
    assert "Save config to redis success" in content
    del async
    os.unlink("test_redis_conf.log")
    async = WheatServer("redis.conf", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % PROJECT_PATH + "/example/",
                               "--static-file-dir %s" % "/static",
                               "--protocol Redis",
                               "--config-source RedisThenFile",
                               "--config-server 127.0.0.1:18000",
                               "--logfile test_redis_conf1.log",
                               "--port 10826", "--stat-port 10827"
                               )
    time.sleep(0.1)
    r = redis.StrictRedis(port=10826)
    assert r.get(1) == '1'
    f = open("test_redis_conf1.log")
    content = f.read(10000)
    f.close()
    assert "get config from redis server sucessful" in content
    assert "Save config to redis success" not in content
    os.unlink("test_redis_conf1.log")
    del async
    async = WheatServer("redis.conf", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % PROJECT_PATH + "/example/",
                               "--static-file-dir %s" % "/static",
                               "--protocol Redis",
                               "--config-source UseFile",
                               "--config-server 127.0.0.1:18000",
                               "--logfile test_redis_conf2.log",
                               "--port 10824", "--stat-port 10825"
                               )
    time.sleep(0.1)
    r = redis.StrictRedis(port=10824)
    assert r.get(1) == '1'
    f = open("test_redis_conf2.log")
    content = f.read(10000)
    f.close()
    assert "get config from file successful" in content
    assert "Save config to redis success" not in content
    assert "get config from redis server sucessful" not in content
    os.unlink("test_redis_conf2.log")
    del async
