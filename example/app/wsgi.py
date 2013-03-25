HELLO_WORLD = b"Hello world!\n"
COMPLEX = b"Hello world!\n" * 20000

def application(environ, start_response):
    """Simplest possible application object"""
    status = '200 OK'
    post = environ['wsgi.input'].read(1000)
    ret = HELLO_WORLD
    if environ['PATH_INFO'] == '/complex':
        ret = COMPLEX
    elif environ['PATH_INFO'] == '/file':
        ret = open('example/static/example.jpg')
        response_headers = [('Content-type', 'img/jpg')]
        start_response(status, response_headers)
        return ret
    elif post:
        ret = post
    response_headers = [('Content-type', 'text/plain'), ('Content-Length', str(len(ret)))]
    start_response(status, response_headers)

    return [ret]

if __name__ == '__main__':
    from gevent.pywsgi import WSGIServer
    print 'Serving on 8088...'
    WSGIServer(('', 8088), application).serve_forever()
