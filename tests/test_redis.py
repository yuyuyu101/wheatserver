from wheatserver_test import WheatServer, PROJECT_PATH, server_socket
import time
import os
import redis

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
