from wheatserver_test import WheatServer, server_socket, construct_command, PROJECT_PATH
import os
import time
import signal

async_server = None

def test_timeout():
    s = server_socket(10828)
    s.send("GET / HTTP/1.1\r\nUser-Agent: Mo")
    s.recv(100)
    os.kill(async_server.exec_pid, signal.SIGUSR1)
    time.sleep(0.1)
    s = server_socket(10829)
    s.send(construct_command("stat", "worker"))
    time.sleep(0.1)
    d = s.recv(100)
    assert "Timeout Request: 1" in d

def setup_module(module):
    global async_server
    async_server = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-module-path %s" % os.path.join(PROJECT_PATH, "src"),
                               "--timeout-seconds 3", "--stat-refresh-time 1")
    time.sleep(0.5)


def teardown_module(module):
    global async_server
    del async_server
