from wheatserver_test import WheatServer, server_socket, construct_command, PROJECT_PATH
import os
import time
import signal

async_server = None

def setup_module(module):
    global async_server
    async_server = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--timeout-seconds 3", "--stat-refresh-time 1",
                               "--protocol Http")
    time.sleep(0.5)


def teardown_module(module):
    global async_server
    del async_server
