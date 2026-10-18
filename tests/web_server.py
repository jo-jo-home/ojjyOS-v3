#!/usr/bin/env python3
"""Local HTTP test server for ojjyOS network/browser tests.
Created by Jonas Lee. Binds 127.0.0.1 only; the guest reaches it through
QEMU user networking at http://10.0.2.2:<port>/. No internet needed.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading

PAGE = b"""<!DOCTYPE html>
<html><head><title>ojjyOS Test Page</title>
<style>body{color:red}</style><script>alert('never runs')</script></head>
<body>
<h1>Hello from the host</h1>
<p>This page is served by <b>tests/web_server.py</b> for the
<a href="/second.html">ojjyOS browser</a> tests &amp; demos.</p>
<ul><li>First item</li><li>Second &lt;item&gt;</li></ul>
<pre>  preformatted   text
    keeps spacing</pre>
<form action="/search" method="get"><input type="text" name="q"><input type="submit" value="Go"></form>
<p><a href="/redirect">Redirect link</a> | <a href="relative/page.html">Relative link</a></p>
</body></html>"""

SECOND = b"""<html><head><title>Second Page</title></head><body>
<h2>Second page</h2><p>You followed a link. <a href="/">Back home</a></p></body></html>"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    requests = []

    def log_message(self, *args):
        pass

    def send_body(self, body, ctype="text/html; charset=utf-8", status=200):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        Handler.requests.append(self.path)
        if self.path in ("/", "/index.html"):
            self.send_body(PAGE)
        elif self.path == "/second.html":
            self.send_body(SECOND)
        elif self.path.startswith("/relative/"):
            self.send_body(b"<html><body><p>Relative OK</p><a href='../second.html'>up</a></body></html>")
        elif self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/second.html")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
        elif self.path == "/chunked":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for part in [b"chunk-one\n", b"chunk-two\n", b"x" * 5000 + b"\n", b"chunk-end\n"]:
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/big":
            body = bytes((i * 7 + (i >> 9)) % 251 for i in range(300000))
            self.send_body(body, "application/octet-stream")
        elif self.path.startswith("/search"):
            q = self.path.split("q=", 1)[-1]
            self.send_body(b"<html><head><title>Results</title></head><body><h1>Search: " + q.encode() +
                           b"</h1></body></html>")
        else:
            self.send_body(b"<html><body><h1>404</h1></body></html>", status=404)


def start():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


if __name__ == "__main__":
    s = start()
    print(f"serving on http://127.0.0.1:{s.server_address[1]}/ (guest: http://10.0.2.2:{s.server_address[1]}/)")
    threading.Event().wait()
