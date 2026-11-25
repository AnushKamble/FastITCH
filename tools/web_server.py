#!/usr/bin/env python3
"""
FastITCH (QD-02) Local Web Terminal Server
Serves the web terminal frontend and provides API endpoints for live benchmark execution.
"""

import http.server
import json
import os
import subprocess
import sys
from pathlib import Path

PORT = 8080
HOST = "0.0.0.0"

ROOT_DIR = Path(__file__).resolve().parent.parent

class FastITCHHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT_DIR), **kwargs)

    def do_GET(self):
        if self.path == "/" or self.path == "":
            self.send_response(302)
            self.send_header("Location", "/web/index.html")
            self.end_headers()
            return
        elif self.path == "/api/status":
            self.handle_api_status()
            return
        elif self.path == "/api/benchmark":
            self.handle_api_benchmark()
            return

        # Ensure correct MIME type for binary and PCAP
        if self.path.endswith(".bin") or self.path.endswith(".pcap"):
            self.extensions_map[".bin"] = "application/octet-stream"
            self.extensions_map[".pcap"] = "application/octet-stream"

        return super().do_GET()

    def handle_api_status(self):
        fixtures = [f.name for f in (ROOT_DIR / "fixtures").glob("*") if f.is_file()]
        bin_exists = (ROOT_DIR / "build_release" / "fastitch").exists() or \
                     (ROOT_DIR / "build_release" / "fastitch.exe").exists()

        status_data = {
            "status": "online",
            "engine": "FastITCH (QD-02)",
            "version": "1.0.0",
            "standard": "NASDAQ TotalView-ITCH 5.0",
            "framing": ["BinaryFILE (2-byte BE)", "MoldUDP64 (20-byte session envelope)"],
            "binary_compiled": bin_exists,
            "fixtures": fixtures
        }

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(status_data, indent=2).encode("utf-8"))

    def handle_api_benchmark(self):
        bench_bin = ROOT_DIR / "build_release" / "fastitch_bench"
        bench_exe = ROOT_DIR / "build_release" / "fastitch_bench.exe"

        bench_target = bench_bin if bench_bin.exists() else bench_exe

        if not bench_target.exists():
            # Check WSL path
            res = subprocess.run(["wsl", "bash", "-c", "test -f build_release/fastitch_bench && echo ok"], capture_output=True, text=True)
            if "ok" in res.stdout:
                proc = subprocess.run(["wsl", "bash", "-c", "./build_release/fastitch_bench --benchmark_format=json"], capture_output=True, text=True)
                out = proc.stdout
                try:
                    bench_json = json.loads(out)
                except Exception:
                    bench_json = {"raw_output": out}
            else:
                bench_json = {"error": "Benchmark executable not built. Run cmake --build build_release"}
        else:
            proc = subprocess.run([str(bench_target), "--benchmark_format=json"], capture_output=True, text=True)
            try:
                bench_json = json.loads(proc.stdout)
            except Exception:
                bench_json = {"raw_output": proc.stdout}

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(bench_json, indent=2).encode("utf-8"))

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    print(f"============================================================")
    print(f"  FastITCH [QD-02] Web Terminal Server Running")
    print(f"  Serving directory: {ROOT_DIR}")
    print(f"  Local Terminal:    http://localhost:{port}/web/index.html")
    print(f"  Direct Entrypoint: http://localhost:{port}/")
    print(f"  API Status:        http://localhost:{port}/api/status")
    print(f"  API Benchmark:     http://localhost:{port}/api/benchmark")
    print(f"============================================================")

    server = http.server.ThreadingHTTPServer((HOST, port), FastITCHHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server.")
        server.server_close()

if __name__ == "__main__":
    main()
