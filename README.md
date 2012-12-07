wheatserver
===========

Full stack asyc IO Web Framework, like the set of Nginx, Gunicorn, Django.  Now Failed, I hope someone can save it!

Install
===========

shell > cd wheatserver
shell > make

Design
===========
Master-multi Worker mode.
Every worker process contains Worker, Protocol, Application three levels.

##Detail Refer
Worker: worker_process.h worker_sync.c
Protocol: protocol.h http_parser.c
Application: application.h, wsgi.c
