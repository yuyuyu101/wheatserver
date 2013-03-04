from wheatserver_test import WheatServer, PROJECT_PATH, server_socket
import os
import time

async_server = None

def test_allowed_extension():
    async = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--static-file-root %s" % os.path.join(PROJECT_PATH, "example/"),
                               "--allowed-extension bmp,gif")
    time.sleep(0.5)
    s = server_socket(10828)
    s.send("GET / HTTP/1.1\r\nHOST: localhost:10828\r\nConnec")
    time.sleep(0.5)
    s.send("tion: close\r\n\r\n")
    a = s.recv(100)
    assert "200" in a
