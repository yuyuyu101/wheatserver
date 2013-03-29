from wheatserver_test import WheatServer, server_socket, construct_command, PROJECT_PATH
import os
import signal
import time

async_server = None

def test_reload():
    async_server = WheatServer("reload.conf", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--protocol Http")
    time.sleep(1)
    os.kill(async_server.exec_pid, signal.SIGHUP)
    s = server_socket(10829)

    s.send(construct_command("stat", "master"))
    assert "Total spawn workers: 8" in s.recv(1000)
