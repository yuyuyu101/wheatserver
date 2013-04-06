#!/usr/bin/python

import code
import sys
import socket

def server_socket(port):
    s = socket.socket()
    s.connect(("127.0.0.1", port))
    return s

def construct_command(*args):
    return "\r\r%s$" % ("\n".join(args))

if __name__ == '__main__':
    print "Wheatserver admin client. Usage: ./client.py [port]"
    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = 10829
    try:
        s = server_socket(port)
    except socket.error:
        print "Connect to port %d failed" % port
    else:
        s.settimeout(0.1)
        while 1:
            try:
                r = code.InteractiveConsole(locals=globals()).raw_input(">>> ")
                command = construct_command(*r.split())
                s.send(command)
                try:
                    a = s.recv(100)
                    while a:
                        print a,
                        a = s.recv(100)
                except socket.timeout:
                    pass
            except EOFError, KeyboardInterrupt:
                break
    print "exit"
