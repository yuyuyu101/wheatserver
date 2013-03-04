HELLO_WORLD = b"Hello world!\n"

def application(environ, start_response):
    """Simplest possible application object"""
    status = '200 OK'
    response_headers = [('Content-type', 'text/plain'), ('Content-Length', '13')]
    a = environ['wsgi.input'].read()
    import pdb; pdb.set_trace()
    start_response(status, response_headers)
    return [HELLO_WORLD]
