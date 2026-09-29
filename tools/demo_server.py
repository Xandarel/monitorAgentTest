#!/usr/bin/env python3
"""Принимает JSON-пакеты агента и печатает их."""

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length)
        text = body.decode("utf-8", errors="replace")
        try:
            packet = json.loads(text)
        except json.JSONDecodeError:
            print(text)
        else:
            payload = packet.get("payload")
            count = len(payload) if isinstance(payload, list) else "?"
            print(
                f"agent_id={packet.get('agent_id')} "
                f"timestamp={packet.get('timestamp')} events={count}"
            )
            print(json.dumps(packet, ensure_ascii=False, indent=2))
        print("-" * 40)

        response = b'{"status":"ok"}\n'
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def log_message(self, fmt, *args):
        print("[http]", fmt % args)


def main():
    parser = argparse.ArgumentParser(description="Локальный приёмник пакетов агента")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"listening on http://{args.host}:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("stopped")


if __name__ == "__main__":
    main()
