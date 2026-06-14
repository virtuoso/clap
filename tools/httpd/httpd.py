#!/usr/bin/env python3
import argparse
import http.server
import os
import socket
import ssl
import subprocess
import sys
import threading
from pathlib import Path
from socketserver import ThreadingMixIn
import mimetypes

# make sure .wasm is correct
mimetypes.add_type("application/wasm", ".wasm")

CORS_HEADERS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, OPTIONS",
    "Access-Control-Allow-Headers": "*",
    "Cache-Control": "no-store, max-age=0, no-cache, must-revalidate",
    # Cross-origin isolation (required for SharedArrayBuffer etc.)
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
    # Optional but nice to have:
    # "Cross-Origin-Resource-Policy": "same-origin",
    # "Origin-Agent-Cluster": "?1",
}

class cors_handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        for k, v in CORS_HEADERS.items():
            self.send_header(k, v)
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

class threaded_https_server(ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

def generate_self_signed_cert(cert_path: Path, key_path: Path):
    print("[*] Generating self-signed certificate (localhost, 127.0.0.1)")

    def run_openssl_cmd(cmd):
        try:
            subprocess.run(cmd, check=True, capture_output=True, text=True)
        except FileNotFoundError:
            print("[!] Error: 'openssl' executable not found in PATH.")
            print("    Please install OpenSSL or ensure it is in your PATH.")
            sys.exit(3)

    # First try modern OpenSSL with -addext; fall back to a temp config if that fails.
    try:
        run_openssl_cmd([
            "openssl", "req",
            "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "365",
            "-nodes",
            "-keyout", str(key_path),
            "-out", str(cert_path),
            "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
        ])
        return
    except subprocess.CalledProcessError:
        pass

    # Fallback path with a temporary config file for SAN
    cfg = (
        "[req]\n"
        "distinguished_name=req\n"
        "x509_extensions=v3_req\n"
        "[v3_req]\n"
        "subjectAltName=DNS:localhost,IP:127.0.0.1\n"
    )
    cfg_path = cert_path.with_suffix(".openssl.cnf")
    cfg_path.write_text(cfg, encoding="utf-8")
    try:
        run_openssl_cmd([
            "openssl", "req",
            "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "365",
            "-nodes",
            "-keyout", str(key_path),
            "-out", str(cert_path),
            "-subj", "/CN=localhost",
            "-extensions", "v3_req",
            "-config", str(cfg_path),
        ])
    except subprocess.CalledProcessError as e:
        print(f"[!] Error: openssl command failed with exit code {e.returncode}")
        print(f"    Command: {' '.join(e.cmd)}")
        if e.stderr:
            print(f"    Stderr:\n{e.stderr.strip()}")
        sys.exit(3)
    finally:
        try:
            cfg_path.unlink()
        except Exception:
            pass

def make_ssl_context(cert_path: Path, key_path: Path, autocert: bool) -> ssl.SSLContext:
    if not cert_path.exists() or not key_path.exists():
        if not autocert:
            print(f"[!] Cert or key missing: {cert_path} / {key_path}")
            print("    Use --autocert to generate a self-signed dev certificate.")
            sys.exit(2)
        generate_self_signed_cert(cert_path, key_path)

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    # Reasonable defaults for local dev
    ctx.options |= ssl.OP_NO_SSLv2 | ssl.OP_NO_SSLv3 | ssl.OP_NO_COMPRESSION
    try:
        ctx.set_ciphers("TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_256_GCM_SHA384")
    except ssl.SSLError:
        pass  # older OpenSSL; ignore
    ctx.load_cert_chain(certfile=str(cert_path), keyfile=str(key_path))
    return ctx

def wss_parse_hostport(value, what):
    host, sep, port = value.rpartition(":")
    if not sep or not host or not port.isdigit():
        print(f"[!] Invalid {what} '{value}', expected ADDR:PORT")
        sys.exit(2)
    return host, int(port)

def wss_pump(src, dst, counter):
    # Byte-transparent: the websocket handshake and frames are opaque here.
    # A blocking sendall() is the backpressure: a slow peer stalls only its
    # own connection, with at most 64 KiB in flight.
    try:
        while True:
            data = src.recv(65536)
            if not data:
                break
            dst.sendall(data)
            counter[0] += len(data)
    except OSError:
        pass
    finally:
        # Propagate the half-close so the backend sees EOF and finishes the
        # client's log file. On the TLS side this sends a bare FIN without a
        # close_notify, which browsers are fine with for a dev setup.
        try:
            dst.shutdown(socket.SHUT_WR)
        except OSError:
            pass

def wss_connection(conn, peer, ssl_ctx, backend):
    who = f"{peer[0]}:{peer[1]}"
    try:
        # Handshake here, in the per-connection thread, so a stalled or
        # cert-rejecting client can't block the accept loop.
        conn.settimeout(10)
        conn = ssl_ctx.wrap_socket(conn, server_side=True)
    except (ssl.SSLError, OSError) as e:
        print(f"[wss] {who} TLS handshake failed: {e}", flush=True)
        conn.close()
        return

    try:
        upstream = socket.create_connection(backend, timeout=5)
    except OSError as e:
        print(f"[wss] {who} no backend at {backend[0]}:{backend[1]}: {e}", flush=True)
        conn.close()
        return

    for s in (conn, upstream):
        s.settimeout(None)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    print(f"[wss] {who} connected -> {backend[0]}:{backend[1]}", flush=True)
    c2s, s2c = [0], [0]
    pumps = [threading.Thread(target=wss_pump, args=(conn, upstream, c2s), daemon=True),
             threading.Thread(target=wss_pump, args=(upstream, conn, s2c), daemon=True)]
    for t in pumps:
        t.start()
    for t in pumps:
        t.join()
    for s in (conn, upstream):
        try:
            s.close()
        except OSError:
            pass
    print(f"[wss] {who} closed (client->server {c2s[0]} B, server->client {s2c[0]} B)", flush=True)

def wss_accept_loop(lsock, ssl_ctx, backend):
    while True:
        try:
            conn, peer = lsock.accept()
        except OSError as e:
            print(f"[wss] accept failed: {e}", flush=True)
            continue
        threading.Thread(target=wss_connection, args=(conn, peer, ssl_ctx, backend),
                         daemon=True).start()

def wss_start(args, ssl_ctx):
    lhost, lport = wss_parse_hostport(args.wss_listen, "--wss-listen")
    backend = wss_parse_hostport(args.wss_backend, "--wss-backend")

    lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        lsock.bind((lhost, lport))
        lsock.listen(16)
    except OSError as e:
        print(f"[!] Error: could not listen on {lhost}:{lport} for wss: {e}")
        sys.exit(1)

    threading.Thread(target=wss_accept_loop, args=(lsock, ssl_ctx, backend),
                     daemon=True).start()
    print(f"[wss] proxying wss://{lhost}:{lport} -> ws://{backend[0]}:{backend[1]}", flush=True)

def parse_args():
    p = argparse.ArgumentParser(description="Minimal HTTPS static server with cross-origin isolation (COOP/COEP) + CORS.")
    p.add_argument("--host", default="127.0.0.1", help="Bind address (default: 127.0.0.1)")
    p.add_argument("--port", type=int, default=8443, help="Port (default: 8443)")
    p.add_argument("--dir", default=".", help="Directory to serve (default: .)")
    p.add_argument("--cert", default="dev-cert.pem", help="Path to TLS cert (default: dev-cert.pem)")
    p.add_argument("--key", default="dev-key.pem", help="Path to TLS private key (default: dev-key.pem)")
    p.add_argument("--autocert", action="store_true", help="Auto-generate a self-signed cert if missing (uses openssl).")
    p.add_argument("--wss-listen", metavar="ADDR:PORT",
                   help="Terminate wss:// here and pipe the bytes to --wss-backend.")
    p.add_argument("--wss-backend", metavar="ADDR:PORT",
                   help="Plaintext websocket backend (the clap server's websocket port).")
    return p.parse_args()

def main():
    args = parse_args()
    web_root = Path(args.dir).resolve()
    cert_path = Path(args.cert).resolve()
    key_path = Path(args.key).resolve()

    if not web_root.is_dir():
        print(f"[!] Not a directory: {web_root}")
        sys.exit(2)

    if bool(args.wss_listen) != bool(args.wss_backend):
        print("[!] --wss-listen and --wss-backend go together")
        sys.exit(2)

    os.chdir(web_root)
    ssl_ctx = make_ssl_context(cert_path, key_path, args.autocert)

    if args.wss_listen:
        wss_start(args, ssl_ctx)

    try:
        httpd = threaded_https_server((args.host, args.port), cors_handler)
        httpd.socket = ssl_ctx.wrap_socket(httpd.socket, server_side=True)

        url_host = "localhost" if args.host in ("127.0.0.1", "0.0.0.0") else args.host
        print(f"Serving HTTPS on https://{url_host}:{args.port} from {web_root}")
        print("COOP/COEP enabled; CORS: *; .wasm served as application/wasm.")
        print("Note: with a self-signed cert, your browser will prompt to trust it once.")
        httpd.serve_forever()
    except OSError as e:
        print(f"[!] Error: Could not start server on {args.host}:{args.port}")
        print(f"    {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nShutting down")

if __name__ == "__main__":
    main()

