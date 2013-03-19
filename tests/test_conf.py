from wheatserver_test import WheatServer, PROJECT_PATH, server_socket
from time import sleep
import os

def test_conf_list():
    async = WheatServer("-t", "%s" % os.path.join(os.path.join(PROJECT_PATH, "tests"), "list_conf.conf"))
    sleep(0.1)
    process_id, status = os.waitpid(async.exec_pid, 0)
    assert status == 0

def test_conf_list1():
    async = WheatServer("-t", "%s" % os.path.join(os.path.join(PROJECT_PATH, "tests"), "list_conf1.conf"))
    sleep(0.1)
    process_id, status = os.waitpid(async.exec_pid, 0)
    assert status == 0
