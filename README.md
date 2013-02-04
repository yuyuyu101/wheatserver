wheatserver(Alpha)
===========

Full stack sync/asyc(wait) IO Web Framework, like the very lightweight of
[uWSGI](http://projects.unbit.it/uwsgi/).

Build
===========

shell > cd wheatserver

shell > cd src

shell > make

Run
===========

-./wheatserver --app-module-path {your app path } --app-module-name {app filename} --app-name {callable object}

Config
===========

See [wheatserver.conf](https://github.com/yuyuyu101/wheatserver/blob/master/wheatserver.conf)

Example(Sample)
===========

<pre>
#sample.py which is in the wheatserver/src
HELLO_WORLD = b"Hello world!\n"

def simple_app(environ, start_response):
    """Simplest possible application object"""
    status = '200 OK'
    response_headers = [('Content-type', 'text/plain')]
    start_response(status, response_headers)
    return [HELLO_WORLD]
</pre>

-./wheatserver --app-module-name sample --app-name simple_app

Example(Django)
===========

-My Django Project Directory:
<pre>
|-signup
   |-wsgi.py
   |-bin
   |-include
   |-lib
   |-signup
   |---activity
   |-----fixtures
   |-----static
   |-------css
   |-------img
   |-------js
   |---assets
   |-----static
   |---benefits
   |-----templatetags
   |---finance
   |-----templatetags
   |---fixtures
   |---logs
   |---match
   |-----......
   |---media
   |-----......
   |---snapboard
   |-----.......
   |---specialist
   |-----static
   |-------css
   |-------js
   |-----templates
   |-------admin
   |---------specialist
   |---static
   |---templates
   |-----......
   |---third_user
   |-----static
   |---settings.py
   |---urls.py
</pre>

wsgi.py at the top of tree is the entry of Django WSGI.

<pre>
import os, sys

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'signup'))
os.environ.setdefault("DJANGO_SETTINGS_MODULE", "signup.settings")

# This application object is used by the development server
# as well as any WSGI server configured to use this file.
from django.core.wsgi import get_wsgi_application
application = get_wsgi_application()
</pre>

-shell> ./wheatserver --app-module-path /Users/abcd/signup/ --app-module-name wsgi --app-name application

Signals for controlling
===========

<pre>
# using reload to send the signal
kill -HUP `cat /tmp/wheatserver.pid`
# using kill to send the signal
kill -INT `cat /tmp/project-master.pid`
</pre>

<table>
   <tr>
      <td>Signal</td>
      <td>Description</td>
   </tr>
   <tr>
      <td>SIGHUP</td>
      <td>reload configuration file, gracefully reload all the workers and the master process</td>
   </tr>
   <tr>
      <td>SIGTERM</td>
      <td>brutally kill all the workers and the master process</td>
   </tr>
   <tr>
      <td>SIGINT</td>
      <td>brutally kill the workers and the master process</td>
   </tr>
   <tr>
      <td>SIGQUIT</td>
      <td>gracefully kill the workers and the master process</td>
   </tr>
   <tr>
      <td>SIGUSR1</td>
      <td>print statistics</td>
   </tr>
   <tr>
      <td>SIGUSR2</td>
      <td>reexec the entire master process and spawn workers</td>
   </tr>
   <tr>
      <td>SIGTTIN</td>
      <td>add one to workernumber</td>
   </tr>
   <tr>
      <td>SIGTTOU</td>
      <td>subtraction of one from workernumber</td>
   </tr>
   <tr>
      <td>SIGWINCH</td>
      <td>only gracefully kill the workers and the master process backend</td>
   </tr>
</table>

Design
===========
Master-multi Worker mode.

Every worker process contains Worker, Protocol, Application three levels.

##Detail Refer

Worker: worker_process.h worker_sync.c

Protocol: protocol.h http_parser.c

Application: application.h, wsgi.c
