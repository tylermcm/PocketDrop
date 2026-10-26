"""Native PC receiver checks against adversarial local HTTP fixtures; no browser or user files."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import subprocess
import threading
import zlib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build/tests/pc_transfer_test.exe"
OFFER = "a" * 32
PAYLOAD = b"PocketDrop native fixture\n" * 4000

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    def log_message(self, *_): pass
    def reply(self, status, data=b"", **headers):
        self.send_response(status)
        self.send_header("Content-Length", str(len(data)))
        for name, value in headers.items(): self.send_header(name, value)
        self.end_headers()
        if data:
            try: self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass
    def do_GET(self):
        mode = self.server.mode
        route = self.path.split("/")[-1].split("?")[0]
        data = b"" if mode == "zero" else PAYLOAD
        if route == "redirected":
            self.server.redirected = True
            self.reply(200, b"should not follow")
        elif route == "info":
            if mode == "redirect":
                self.reply(302, Location=f"http://{self.server.server_address[0]}:{self.server.server_address[1]}/redirected")
                return
            name = "../../escape.txt" if mode == "traversal" else "empty.txt" if mode == "zero" else "payload.txt"
            size = "18446744073709551616" if mode == "overflow" else str(2**63-1) if mode == "disk" else str(len(data))
            self.reply(200, f"PDPC1\n{size}\n{name}".encode())
        elif route == "file":
            self.server.downloaded = True
            if mode == "closed": self.reply(404); return
            if mode == "truncated":
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data[:10])
                self.close_connection = True
                return
            self.reply(200, data)
        elif route in ("verify", "complete"):
            actual = self.path.split("crc=")[-1]
            good = mode != "checksum" and actual == str(zlib.crc32(data))
            if route == "complete" and good: self.server.completed = True
            self.reply(204 if good else 422)
        elif route == "cancel": self.reply(204)
        else: self.reply(404)

def check():
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect(("192.0.2.1", 9))
    ip = probe.getsockname()[0]
    probe.close()
    for mode in ("success", "zero", "traversal", "overflow", "disk", "truncated", "checksum", "closed", "redirect"):
        server = ThreadingHTTPServer((ip, 0), Handler)
        server.mode, server.downloaded, server.completed, server.redirected = mode, False, False, False
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        inbox = ROOT / "build" / ("pc-fixture-" + mode)
        inbox.mkdir(exist_ok=True)
        for item in inbox.iterdir():
            if item.is_file(): item.unlink()
        (inbox / "payload.txt").write_bytes(b"existing content")
        try:
            result = subprocess.run([str(EXE), ip, str(server.server_address[1]), OFFER, str(inbox)],
                                    capture_output=True, text=True, timeout=25)
            expected = mode in ("success", "zero")
            assert (result.returncode == 0) == expected, (mode, result.returncode, result.stdout, result.stderr)
            assert (inbox / "payload.txt").read_bytes() == b"existing content", mode
            assert not list(inbox.glob("*.pcpart")), mode
            if expected:
                name = "empty.txt" if mode == "zero" else "payload (2).txt"
                expected_data = b"" if mode == "zero" else PAYLOAD
                assert hashlib.sha256((inbox / name).read_bytes()).digest() == hashlib.sha256(expected_data).digest()
                assert server.completed
            else: assert len(list(inbox.iterdir())) == 1, mode
            if mode in ("traversal", "overflow", "disk", "redirect"): assert not server.downloaded
            assert not server.redirected, "PC credentials must never follow redirects"
            print(mode + ": native receiver OK")
        finally:
            server.shutdown(); server.server_close(); thread.join()
    report = json.loads((ROOT / "build/pc-test-result.json").read_text())
    with zipfile.ZipFile(ROOT / report["zip"]) as archive:
        assert archive.testzip() is None
        for entry in archive.infolist():
            if not entry.is_dir():
                assert archive.read(entry) == (ROOT / report["source"] / entry.filename).read_bytes()
        assert "Photos/Empty/" in archive.namelist()
    print("Native ZIP CRC, folder structure, Unicode names and source byte comparison passed")

if __name__ == "__main__": check()
