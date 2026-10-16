#!/usr/bin/env python3
"""
Autonomous browser test for clap wasm builds.

Serves a build over TLS (tools/httpd/httpd.py: COOP/COEP + wasm mime), drives a
browser over the DevTools protocol, and collects what a human would otherwise
have to eyeball: console output (the engine's stdout lands there), the actual
frame rate, and a screenshot.

No third-party python packages: the CDP client speaks websockets directly.

  webtest.py --dir build/emtest/demo/ldjam56 --chrome /usr/bin/google-chrome \
      --window-position 0,0 --seconds 20 --out /tmp/run
  webtest.py --renderer webgl ...      # disables WebGPU -> engine falls back
"""
import argparse
import base64
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

HTTPD_DIR = Path(__file__).resolve().parent.parent / "httpd"


# ---------------------------------------------------------------- websocket
class WS:
    """Minimal RFC6455 client: text frames, no extensions, no fragmentation."""

    def __init__(self, url, timeout=5):
        m = re.match(r"ws://([^:/]+):(\d+)(/.*)", url)
        if not m:
            raise ValueError(f"bad ws url: {url}")
        host, port, path = m.group(1), int(m.group(2)), m.group(3)

        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\n"
            f"Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        )
        self.sock.sendall(req.encode())

        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("ws handshake: connection closed")
            buf += chunk
        if b"101" not in buf.split(b"\r\n")[0]:
            raise ConnectionError(f"ws handshake failed: {buf.split(b'\r\n')[0]}")
        self.buf = buf.split(b"\r\n\r\n", 1)[1]

    def send(self, data: str):
        payload = data.encode()
        n = len(payload)
        hdr = bytearray([0x81])
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr.append(0x80 | 126)
            hdr += struct.pack(">H", n)
        else:
            hdr.append(0x80 | 127)
            hdr += struct.pack(">Q", n)
        mask = os.urandom(4)
        hdr += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(hdr) + masked)

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("ws: connection closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self, timeout=None):
        """One text frame, or None on timeout."""
        self.sock.settimeout(timeout)
        try:
            b0, b1 = self._read(2)
        except (socket.timeout, TimeoutError):
            return None
        opcode = b0 & 0x0F
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack(">H", self._read(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self._read(8))[0]
        if b1 & 0x80:                       # server frames shouldn't be masked
            mask = self._read(4)
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(self._read(n)))
        else:
            payload = self._read(n)
        if opcode == 0x8:
            raise ConnectionError("ws: server closed")
        if opcode == 0x9:                   # ping -> pong
            return self.recv(timeout)
        return payload.decode("utf-8", "replace")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# --------------------------------------------------------------------- CDP
class CDP:
    def __init__(self, ws_url):
        self.ws = WS(ws_url)
        self.id = 0
        self.events = []

    def send(self, method, **params):
        self.id += 1
        self.ws.send(json.dumps({"id": self.id, "method": method, "params": params}))
        return self.id

    def wait(self, msg_id, timeout=30):
        """Pump until the reply to msg_id arrives; stash events meanwhile."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            raw = self.ws.recv(timeout=max(0.1, deadline - time.time()))
            if raw is None:
                continue
            msg = json.loads(raw)
            if msg.get("id") == msg_id:
                if "error" in msg:
                    raise RuntimeError(f"CDP error: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)
        raise TimeoutError(f"CDP: no reply to #{msg_id}")

    def call(self, method, timeout=30, **params):
        return self.wait(self.send(method, **params), timeout)

    def pump(self, seconds):
        """Collect events for a while."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            raw = self.ws.recv(timeout=max(0.05, min(0.5, deadline - time.time())))
            if raw is None:
                continue
            msg = json.loads(raw)
            if "method" in msg:
                self.events.append(msg)


def fmt_console(msg) -> str | None:
    m = msg.get("method")
    p = msg.get("params", {})
    if m == "Runtime.consoleAPICalled":
        parts = []
        for a in p.get("args", []):
            if "value" in a:
                parts.append(str(a["value"]))
            elif a.get("description"):
                parts.append(a["description"])
            else:
                parts.append(a.get("type", "?"))
        return f"[{p.get('type', 'log')}] " + " ".join(parts)
    if m == "Log.entryAdded":
        e = p.get("entry", {})
        return f"[{e.get('level')}/{e.get('source')}] {e.get('text', '')}"
    if m == "Runtime.exceptionThrown":
        d = p.get("exceptionDetails", {})
        return f"[exception] {d.get('text')} {(d.get('exception') or {}).get('description', '')}"
    return None


FPS_JS = """
new Promise(resolve => {
    let frames = 0;
    const t0 = performance.now();
    const tick = () => {
        frames++;
        const dt = performance.now() - t0;
        if (dt < %d) requestAnimationFrame(tick);
        else resolve({ fps: frames / (dt / 1000), frames: frames, ms: dt });
    };
    requestAnimationFrame(tick);
})
"""

PROBE_JS = """
(() => {
    const c = document.querySelector('canvas');
    return {
        gpu: typeof navigator.gpu !== 'undefined',
        canvas: c ? (c.width + 'x' + c.height) : 'none',
        ctx_webgl2: c ? !!c.getContext('webgl2') : false,
        title: document.title,
        screen: screen.width + 'x' + screen.height,
        dpr: devicePixelRatio,
    };
})()
"""


def wait_port(port, timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.2)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True, help="directory to serve (build output)")
    ap.add_argument("--chrome", required=True, help="path to the Chrome binary")
    ap.add_argument("--page", default="index.html")
    ap.add_argument("--seconds", type=float, default=20, help="run time before probing")
    ap.add_argument("--fps-window", type=float, default=3, help="fps measurement window")
    ap.add_argument("--renderer", choices=["wgpu", "webgl"], default="wgpu")
    ap.add_argument("--out", default="/tmp/clap-webtest", help="output prefix")
    ap.add_argument("--window-size", default="1280,800")
    ap.add_argument("--window-position", required=True,
                    help="pin the window to a display: rAF runs at that display's refresh rate")
    ap.add_argument("--http-port", type=int, default=8443)
    ap.add_argument("--cdp-port", type=int, default=9222)
    ap.add_argument("--keep-open", action="store_true")
    ap.add_argument("--press", default=None, help="key to press before probing (e.g. Escape)")
    ap.add_argument("--click", action="store_true", help="click the canvas first (focus)")
    args = ap.parse_args()

    serve_dir = Path(args.dir).resolve()
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    httpd_py = HTTPD_DIR / "httpd.py"
    if not httpd_py.exists():
        httpd_py = Path("tools/httpd/httpd.py").resolve()

    certdir = Path(tempfile.mkdtemp(prefix="clap-cert-"))
    profile = Path(tempfile.mkdtemp(prefix="clap-chrome-"))
    httpd = chrome = None
    try:
        httpd = subprocess.Popen(
            [sys.executable, str(httpd_py), "--dir", str(serve_dir),
             "--port", str(args.http_port), "--autocert",
             "--cert", str(certdir / "cert.pem"), "--key", str(certdir / "key.pem")],
            # nobody reads the server's output: a pipe would fill up and block it
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not wait_port(args.http_port):
            raise SystemExit("httpd did not come up")

        flags = [
            args.chrome,
            f"--user-data-dir={profile}",
            f"--remote-debugging-port={args.cdp_port}",
            "--no-first-run", "--no-default-browser-check", "--use-mock-keychain",
            # the Linux side of --use-mock-keychain: without it, a fresh profile
            # waits on the desktop's keyring (KWallet) and never navigates
            "--password-store=basic",
            "--ignore-certificate-errors", "--allow-insecure-localhost",
            "--autoplay-policy=no-user-gesture-required",
            # keep the frame loop running at full speed even unfocused/occluded
            "--disable-background-timer-throttling",
            "--disable-renderer-backgrounding",
            "--disable-backgrounding-occluded-windows",
            f"--window-size={args.window_size}",
        ]
        flags.append(f"--window-position={args.window_position}")
        if args.renderer == "wgpu":
            flags += ["--enable-unsafe-webgpu"]
        else:
            flags += ["--disable-features=WebGPU"]
        flags += ["about:blank"]

        chrome = subprocess.Popen(flags, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not wait_port(args.cdp_port):
            raise SystemExit("chrome devtools port did not come up")

        targets = json.loads(urllib.request.urlopen(
            f"http://127.0.0.1:{args.cdp_port}/json/list", timeout=10).read())
        page = next(t for t in targets if t["type"] == "page")
        cdp = CDP(page["webSocketDebuggerUrl"])

        # enable before navigating so nothing is missed
        cdp.call("Runtime.enable")
        cdp.call("Log.enable")
        cdp.call("Page.enable")

        if args.renderer == "webgl":
            # the chrome flag doesn't remove navigator.gpu; the engine picks
            # wgpu whenever it's present, so take it away before page scripts
            cdp.call("Page.addScriptToEvaluateOnNewDocument",
                     source="Object.defineProperty(navigator, 'gpu', { configurable: true,"
                            " value: { requestAdapter: () => Promise.resolve(null),"
                            " getPreferredCanvasFormat: () => 'bgra8unorm',"
                            " wgslLanguageFeatures: new Set() } });")

        url = f"https://localhost:{args.http_port}/{args.page}"
        print(f"[*] {args.renderer}: {url} for {args.seconds}s")
        cdp.call("Page.navigate", url=url)
        cdp.pump(args.seconds)

        if args.click:
            for t in ("mousePressed", "mouseReleased"):
                cdp.call("Input.dispatchMouseEvent", type=t, x=640, y=400,
                         button="left", clickCount=1)
            time.sleep(0.5)
        if args.press:
            code = {"Escape": 27, "Enter": 13, "Tab": 9}.get(args.press, 0)
            for t in ("keyDown", "keyUp"):
                cdp.call("Input.dispatchKeyEvent", type=t, key=args.press,
                         code=args.press, windowsVirtualKeyCode=code,
                         nativeVirtualKeyCode=code)
            cdp.pump(2)

        probe = fps = None
        try:
            probe = cdp.call("Runtime.evaluate", expression=PROBE_JS,
                             returnByValue=True).get("result", {}).get("value")
            fps = cdp.call("Runtime.evaluate", expression=FPS_JS % int(args.fps_window * 1000),
                           awaitPromise=True, returnByValue=True,
                           timeout=args.fps_window + 20).get("result", {}).get("value")
        except Exception as e:                              # keep logs even if probing dies
            print(f"[!] probe failed: {e}")

        shot = None
        try:
            data = cdp.call("Page.captureScreenshot", format="png").get("data")
            shot = out.with_suffix(".png")
            shot.write_bytes(base64.b64decode(data))
        except Exception as e:
            print(f"[!] screenshot failed: {e}")

        lines = [s for s in (fmt_console(m) for m in cdp.events) if s]
        log_path = out.with_suffix(".log")
        log_path.write_text("\n".join(lines), encoding="utf-8")

        errors = [l for l in lines if re.search(r"error|fail|invalid|warn|exception", l, re.I)]
        print(f"[*] console lines: {len(lines)} -> {log_path}")
        print(f"[*] probe: {probe}")
        print(f"[*] fps: {fps}")
        if shot:
            print(f"[*] screenshot: {shot}")
        if errors:
            print(f"[*] {len(errors)} error-ish lines, first 40:")
            for l in errors[:40]:
                print("    " + l[:300])

        if args.keep_open:
            input("[*] enter to close...")
    finally:
        for p in (chrome, httpd):
            if p:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
        shutil.rmtree(profile, ignore_errors=True)
        shutil.rmtree(certdir, ignore_errors=True)


if __name__ == "__main__":
    main()
