#!/usr/bin/env python3
"""Local OpenGauge configuration portal prototype. Run from the repository root."""
from __future__ import annotations

import argparse
import json
import sys
import tempfile
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WEB = HERE / 'web'
LOCAL_CONFIG = HERE / 'local_config.json'
sys.path.insert(0, str(ROOT / 'tools'))

from config_model import default_copy, validate  # noqa: E402
from gauge_preview import render_config_preview  # noqa: E402

def load_config() -> dict:
    if LOCAL_CONFIG.exists():
        try:
            config=json.loads(LOCAL_CONFIG.read_text(encoding='utf-8'))
            if not validate(config): return config
        except (OSError, json.JSONDecodeError): pass
    return default_copy()

def write_json(handler: SimpleHTTPRequestHandler, status: int, value: object) -> None:
    data=json.dumps(value, indent=2).encode('utf-8')
    handler.send_response(status)
    handler.send_header('Content-Type', 'application/json; charset=utf-8')
    handler.send_header('Content-Length', str(len(data)))
    handler.end_headers(); handler.wfile.write(data)

class PortalHandler(SimpleHTTPRequestHandler):
    def translate_path(self, path: str) -> str:
        requested=urlparse(path).path
        if requested in ('/', '/index.html'): return str(WEB / 'index.html')
        return str(WEB / requested.lstrip('/'))

    def read_body(self) -> object:
        length=int(self.headers.get('Content-Length','0'))
        if length > 32768: raise ValueError('Request is larger than 32 KB.')
        return json.loads(self.rfile.read(length).decode('utf-8'))

    def do_GET(self) -> None:
        path=urlparse(self.path).path
        if path == '/api/config': write_json(self, HTTPStatus.OK, load_config()); return
        if path == '/api/default-config': write_json(self, HTTPStatus.OK, default_copy()); return
        super().do_GET()

    def do_PUT(self) -> None:
        if urlparse(self.path).path != '/api/config': self.send_error(HTTPStatus.NOT_FOUND); return
        try:
            config=self.read_body(); errors=validate(config)
        except (ValueError, json.JSONDecodeError) as error:
            write_json(self, HTTPStatus.BAD_REQUEST, {'errors':[str(error)]}); return
        if errors: write_json(self, HTTPStatus.BAD_REQUEST, {'errors':errors}); return
        LOCAL_CONFIG.write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')
        write_json(self, HTTPStatus.OK, {'ok':True, 'message':'Saved to local_config.json.'})

    def do_POST(self) -> None:
        if urlparse(self.path).path != '/api/preview': self.send_error(HTTPStatus.NOT_FOUND); return
        try:
            request=self.read_body(); config=request['config']; index=int(request['gaugeIndex'])
            samples=request.get('samples', {}); errors=validate(config)
            if not 0 <= index < len(config['gauges']): errors.append('Select a valid gauge.')
            if errors: write_json(self, HTTPStatus.BAD_REQUEST, {'errors':errors}); return
            with tempfile.TemporaryDirectory() as directory:
                output=Path(directory) / 'preview.png'
                render_config_preview(config, index, samples, output, scale=3)
                data=output.read_bytes()
        except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
            write_json(self, HTTPStatus.BAD_REQUEST, {'errors':[str(error)]}); return
        except Exception as error:
            write_json(self, HTTPStatus.INTERNAL_SERVER_ERROR, {'errors':[str(error)]}); return
        self.send_response(HTTPStatus.OK)
        self.send_header('Content-Type','image/png'); self.send_header('Content-Length',str(len(data)))
        self.end_headers(); self.wfile.write(data)

def main() -> None:
    parser=argparse.ArgumentParser(description='OpenGauge local configuration portal')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8000)
    args=parser.parse_args()
    server=ThreadingHTTPServer((args.host,args.port), PortalHandler)
    print(f'Open http://{args.host}:{args.port}')
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close()

if __name__ == '__main__': main()
