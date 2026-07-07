#!/usr/bin/env python3
"""
BMA180 UDP -> Web display server.

Run this on your PC (same Wi-Fi network as the STM32 board).
It listens for UDP packets like:  X:+123,Y:-45,Z:+980\n
and serves a live-updating web page. Open http://<PC-IP>:8080 on your
phone's browser (phone must be on the same Wi-Fi network).

Usage:
    python3 bma180_server.py
"""

import socket
import threading
import time
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UDP_PORT = 5000        # must match SERVER_UDP_PORT in wifi_conf.hpp
HTTP_PORT = 8080

latest = {"x": 0, "y": 0, "z": 0, "last_update": 0.0, "packets": 0}
lock = threading.Lock()


def udp_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_PORT))
    print(f"[UDP] Listening on port {UDP_PORT} ...")
    while True:
        data, addr = sock.recvfrom(256)
        try:
            text = data.decode("ascii", errors="ignore").strip()
            parts = dict(p.split(":") for p in text.split(","))
            x = int(parts["X"])
            y = int(parts["Y"])
            z = int(parts["Z"])
            with lock:
                latest["x"] = x
                latest["y"] = y
                latest["z"] = z
                latest["last_update"] = time.time()
                latest["packets"] += 1
        except Exception as e:
            print(f"[UDP] Bad packet from {addr}: {data!r} ({e})")


PAGE = """<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BMA180 Live</title>
<style>
  body { font-family: sans-serif; background:#111; color:#eee; text-align:center; }
  h1 { font-size: 1.3em; margin-top: 1em; }
  .val { font-size: 2.5em; margin: 0.3em; }
  .x { color:#ff6b6b; } .y { color:#6bff8c; } .z { color:#6bb8ff; }
  #status { color:#888; font-size:0.9em; }
</style>
</head>
<body>
  <h1>BMA180 Accelerometer</h1>
  <div class="val x">X: <span id="x">--</span></div>
  <div class="val y">Y: <span id="y">--</span></div>
  <div class="val z">Z: <span id="z">--</span></div>
  <div id="status">waiting for data...</div>
<script>
async function poll() {
  try {
    const r = await fetch('/data');
    const d = await r.json();
    document.getElementById('x').textContent = d.x;
    document.getElementById('y').textContent = d.y;
    document.getElementById('z').textContent = d.z;
    const age = (Date.now()/1000 - d.last_update).toFixed(1);
    document.getElementById('status').textContent =
      d.last_update ? `updated ${age}s ago (${d.packets} packets)` : 'no data yet';
  } catch (e) {
    document.getElementById('status').textContent = 'connection error';
  }
  setTimeout(poll, 150);
}
poll();
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # quiet

    def do_GET(self):
        if self.path == "/data":
            with lock:
                payload = json.dumps(latest).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        else:
            payload = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)


def main():
    t = threading.Thread(target=udp_listener, daemon=True)
    t.start()

    server = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)

    # Print the LAN IP so you know what to type on your phone
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
    except Exception:
        local_ip = "127.0.0.1"
    finally:
        s.close()

    print(f"[HTTP] Serving on http://{local_ip}:{HTTP_PORT}  (open this on your phone)")
    print(f"[HTTP] Make sure SERVER_IP_1..4 in wifi_conf.hpp match: {local_ip}")
    server.serve_forever()


if __name__ == "__main__":
    main()
