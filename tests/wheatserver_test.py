import httplib
import os
import signal
import socket
import time
import pytest

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

server = WheatServer("", "--app-module-path %s" % os.path.join(PROJECT_PATH, "src"))

@pytest.fixture
def server_socket():
    s = socket.socket()
    s.connect(("127.0.0.1", 10829))
    return s

def construct_command(*args):
    return "\r\r%s$" % ("\n".join(args))

def teardown_module(module):
    global server
    del server

def test_config_command(server_socket):
    server_socket.send(construct_command("config", "logfile-level"))
    assert server_socket.recv(100) == "logfile-level: DEBUG"

def test_stat_accuracy(server_socket):
    conn = httplib.HTTPConnection("127.0.0.1", 10828);
    for i in range(100):
        conn.request("GET", "/")
        r1 = conn.getresponse()
        assert r1.status == 200
    os.kill(server.exec_pid, signal.SIGUSR1)
    time.sleep(0.1)
    server_socket.send(construct_command("stat", "worker"))
    assert "Total Connection: 100" in server_socket.recv(1000)
