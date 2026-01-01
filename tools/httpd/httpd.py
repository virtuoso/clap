#!/usr/bin/env python3
import argparse
import http.server
import os
import ssl
import subprocess
import sys
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

def parse_args():
    p = argparse.ArgumentParser(description="Minimal HTTPS static server with cross-origin isolation (COOP/COEP) + CORS.")
    p.add_argument("--host", default="127.0.0.1", help="Bind address (default: 127.0.0.1)")
    p.add_argument("--port", type=int, default=8443, help="Port (default: 8443)")
    p.add_argument("--dir", default=".", help="Directory to serve (default: .)")
    p.add_argument("--cert", default="dev-cert.pem", help="Path to TLS cert (default: dev-cert.pem)")
    p.add_argument("--key", default="dev-key.pem", help="Path to TLS private key (default: dev-key.pem)")
    p.add_argument("--autocert", action="store_true", help="Auto-generate a self-signed cert if missing (uses openssl).")
    return p.parse_args()

def main():
    args = parse_args()
    web_root = Path(args.dir).resolve()
    cert_path = Path(args.cert).resolve()
    key_path = Path(args.key).resolve()

    if not web_root.is_dir():
        print(f"[!] Not a directory: {web_root}")
        sys.exit(2)

    os.chdir(web_root)
    try:
        httpd = threaded_https_server((args.host, args.port), cors_handler)
        httpd.socket = make_ssl_context(cert_path, key_path, args.autocert).wrap_socket(httpd.socket, server_side=True)

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

