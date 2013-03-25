from wheatserver_test import WheatServer, PROJECT_PATH, server_socket
import os
import time
import requests

POST_DATA = b"""POST /asdf HTTP/1.1\r\nHost: 127.0.0.1:10828\r\nContent-Length: 200\r\nContent-Type: multipart/form-data; boundary=25510934abe14960a7309cc7a2c790d8\r\nAccept-Encoding: gzip, deflate, compress\r\nAccept: */*\r\nUser-Agent: python-requests/1.1.0 CPython/2.7.2 Darwin/12.2.0\r\n\r\n--25510934abe14960a7309cc7a2c790d8\r\nContent-Disposition: form-data; name=\"file\"; filename=\"/Users/wanghaomai/Downloads/1.txt\"\r\nContent-Type: text/plain\r\n\r\n1234\n\r\n--25510934abe14960a7309cc7a2c790d8--\r\n"""

def test_get_two():
    async = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % os.path.join(PROJECT_PATH, "example/"),
                               "--allowed-extension bmp,gif",
                               "--protocol Http")
    time.sleep(0.1)
    s = server_socket(10828)
    s.send("GET / HTTP/1.1\r\nHOST: 127.0.0.1:10828\r\nConnec")
    time.sleep(0.2)
    s.send("tion: close\r\n\r\n")
    a = s.recv(100)
    assert "200" in a

def test_post_file():
    async = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % os.path.join(PROJECT_PATH, "example/"),
                               "--allowed-extension bmp,gif",
                               "--protocol Http")
    time.sleep(0.1)
    s = server_socket(10828)
    s.settimeout(0.5)
    s.send(POST_DATA[:302])
    time.sleep(0.1)
    try:
        a = s.recv(100)
    except:
        pass
    else:
        assert "" == a
    s.send(POST_DATA[302:])
    a = s.recv(1000)
    assert "200" in a and "1234" in a

def test_document_root_and_static_file_dir():
    async = WheatServer("", "--worker-type %s" % "AsyncWorker",
                               "--app-project-path %s" % os.path.join(PROJECT_PATH, "example"),
                               "--document-root %s" % PROJECT_PATH + "/example/",
                               "--static-file-dir %s" % "/static",
                               "--allowed-extension bmp,gif,jpg",
                               "--protocol Http")
    time.sleep(0.1)
    r = requests.get("http://127.0.0.1:10828/static/example.jpg",timeout=1)
    assert 200 == r.status_code
