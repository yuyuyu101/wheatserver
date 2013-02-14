import httplib
import os
import signal
import socket
import time

PROJECT_PATH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXECFILE = os.path.join(os.path.join(PROJECT_PATH, "src"), "wheatserver")

class WheatServer(object):
    def __init__(self, conf_file="", *options):
        assert os.access(EXECFILE, os.F_OK)
        self.exec_pid = os.fork()
        if not conf_file:
            conf_file = os.path.join(PROJECT_PATH, "wheatserver.conf")
        if not self.exec_pid:
            os.execl(EXECFILE, EXECFILE, conf_file, *options)

    def __del__(self):
        os.kill(self.exec_pid, signal.SIGQUIT);

sync_server = WheatServer("", "--port 10826", "--stat-port 10827",
                         "--worker-type %s" % "SyncWorker",
                       "--app-module-path %s" % os.path.join(PROJECT_PATH, "src"))

async_server = WheatServer("", "--worker-type %s" % "AsyncWorker",
                       "--app-module-path %s" % os.path.join(PROJECT_PATH, "src"))

def server_socket(port):
    s = socket.socket()
    s.connect(("127.0.0.1", port))
    return s

def construct_command(*args):
    return "\r\r%s$" % ("\n".join(args))

def pytest_generate_tests(metafunc):
    metafunc.parametrize(('port',), [(10829,),(10827,),])

def test_config_command(port):
    s = server_socket(port)
    s.send(construct_command("config", "logfile-level"))
    assert s.recv(100) == "logfile-level: DEBUG"

def test_stat_accuracy(port):
    conn = httplib.HTTPConnection("127.0.0.1", port-1);
    for i in range(100):
        conn.request("GET", "/")
        r1 = conn.getresponse()
        assert r1.status == 200
    os.kill(sync_server.exec_pid, signal.SIGUSR1)
    os.kill(async_server.exec_pid, signal.SIGUSR1)
    time.sleep(0.1)
    s = server_socket(port)
    s.send(construct_command("stat", "worker"))
    assert "Total Connection: 100" in s.recv(1000)

def test_reload(port):
    s = server_socket(port)
    if port == 10827:
        os.kill(sync_server.exec_pid, signal.SIGHUP)
    else:
        os.kill(async_server.exec_pid, signal.SIGHUP)

    conn = httplib.HTTPConnection("127.0.0.1", port-1);
    for i in range(10):
        conn.request("GET", "/")
        r1 = conn.getresponse()
        assert r1.status == 200


def teardown_module(module):
    global sync_server, async_server
    del sync_server, async_server
