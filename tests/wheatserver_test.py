import httplib
import os
import signal

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

def teardown_module(module):
    global server
    del server

def test_stat_accuracy(capsys):
    conn = httplib.HTTPConnection("127.0.0.1", 10828);
    for i in range(1000):
        conn.request("GET", "/")
        r1 = conn.getresponse()
        assert r1.status == 200
