#!/usr/bin/env python3
"""Serve only the public BLE lab files on localhost; no uploads or key storage."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import mimetypes
mimetypes.add_type('text/javascript','.mjs')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',type=int,default=8765)
args=parser.parse_args()
root=Path(__file__).resolve().parent/'ble-signer-lab'
class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; media-src 'self' blob:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        super().end_headers()
with ThreadingHTTPServer(('127.0.0.1',args.port),partial(Handler,directory=root)) as server:
    print(f'BLE lab: http://localhost:{args.port} (Ctrl-C to stop)',flush=True)
    server.serve_forever()
