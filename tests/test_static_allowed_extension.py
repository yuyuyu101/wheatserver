from wheatserver_test import WheatServer, PROJECT_PATH
import os
import time
import httplib

async_server = None

def test_allowed_extension():
    async_server = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % os.path.join(PROJECT_PATH, "example/"),
                               "--allowed-extension bmp,gif",
                               "--protocol Http")
    time.sleep(0.5)
    conn = httplib.HTTPConnection("127.0.0.1", 10828, timeout=10);
    conn.request("GET", "/static/example.jpg")
    r1 = conn.getresponse()
    assert r1.status == 404

def test_allowed_extension2():
    async_server = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % os.path.join(PROJECT_PATH, "example/"),
                               "--allowed-extension jpg",
                               "--protocol Http")
    time.sleep(0.5)
    conn = httplib.HTTPConnection("127.0.0.1", 10828, timeout=10);
    conn.request("GET", "/static/example.jpg")
    r1 = conn.getresponse()
    assert r1.status == 200
    conn = httplib.HTTPConnection("127.0.0.1", 10828, timeout=10);
    conn.request("GET", "/static/")
    r1 = conn.getresponse()
    assert r1.status == 404

def test_allowed_extension3():
    async_server = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % os.path.join(PROJECT_PATH, "example/"),
                               "--allowed-extension *",
                               "--protocol Http")
    time.sleep(0.5)
    conn = httplib.HTTPConnection("127.0.0.1", 10828, timeout=10);
    conn.request("GET", "/static/example.jpg")
    r1 = conn.getresponse()
    assert r1.status == 200
    del async_server
