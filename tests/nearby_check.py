"""Native PC receiver checks against adversarial local HTTP fixtures; no browser or user files.

The fixture is a sender that speaks PC Share v2 (X25519 pairing, ChaCha20-Poly1305 frames) so the receiver can be given
well-formed and hostile behaviour: bad names and sizes, damaged, cut-off or foreign-keyed streams, redirects and so on.
Needs the `cryptography` package (pip install cryptography).
"""
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import zlib
import zipfile

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get("PC_TRANSFER_TEST") or next(
    (p for p in (ROOT / "build/tests/pc_transfer_test.exe", ROOT / "build/tests-linux/pc_transfer_test", ROOT / "build/tests/pc_transfer_test") if p.exists()),
    ROOT / "build/tests/pc_transfer_test.exe"))
OFFER = "a" * 32
PAYLOAD = b"PocketDrop native fixture\n" * 4000
FRAME = 64 * 1024
SALT = b"PocketDrop PC Share v2"


def raw(key):
    return key.public_key().public_bytes_raw()


def hmac256(key, msg):
    return hmac.new(key, msg, hashlib.sha256).digest()


def nonce(counter):
    return b"\0\0\0\0" + struct.pack("<Q", counter)


def derive(shared, pk_sender, pk_receiver):
    prk = hmac256(SALT, shared)
    ctx = pk_sender + pk_receiver
    return hmac256(prk, b"key" + ctx), hmac256(prk, b"claim" + ctx).hex()[:32]


def frames(key, data, tamper=False):
    out = bytearray()
    counter = 0
    for i in range(0, len(data), FRAME):
        chunk = data[i:i + FRAME]
        header = struct.pack("<I", len(chunk))
        counter += 1
        out += header + ChaCha20Poly1305(key).encrypt(nonce(counter), chunk, header)
    counter += 1
    header = struct.pack("<I", 0)
    out += header + ChaCha20Poly1305(key).encrypt(nonce(counter), b"", header)
    if tamper:
        out[len(out) // 2] ^= 1
    return bytes(out)


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
        server = self.server
        mode = server.mode
        route = self.path.split("/")[-1].split("?")[0]
        data = b"" if mode == "zero" else PAYLOAD
        claimed = self.headers.get("X-PocketDrop-Receiver", "")
        if route == "redirected":
            server.redirected = True
            self.reply(200, b"should not follow")
        elif route == "hello":
            if mode == "redirect":
                self.reply(302, Location=f"http://{server.server_address[0]}:{server.server_address[1]}/redirected")
                return
            peer = bytes.fromhex(self.headers.get("X-PocketDrop-Key", ""))
            mine = X25519PrivateKey.generate()
            shared = mine.exchange(X25519PublicKey.from_public_bytes(peer))
            server.key, server.claim = derive(shared, raw(mine), peer)
            if mode == "stranger":                       # seals with a key the receiver does not share
                server.key = os.urandom(32)
            self.reply(200, raw(mine).hex().encode())
        elif claimed != getattr(server, "claim", None):
            self.reply(404)
        elif route == "status":
            self.reply(200, b"declined" if mode == "declined" else b"accepted")
        elif route == "info":
            name = "../../escape.txt" if mode == "traversal" else "empty.txt" if mode == "zero" else "payload.txt"
            size = "18446744073709551616" if mode == "overflow" else str(2**63 - 1) if mode == "disk" else str(len(data))
            sealed = ChaCha20Poly1305(server.key).encrypt(nonce(0), f"PDPC1\n{size}\n{name}".encode(), b"pdpc2 info")
            self.reply(200, sealed)
        elif route == "file":
            server.downloaded = True
            if mode == "closed": self.reply(404); return
            body = frames(server.key, data, tamper=mode == "tampered")
            if mode == "truncated":
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body[:10])
                self.close_connection = True
                return
            self.reply(200, body)
        elif route in ("verify", "complete"):
            actual = self.path.split("crc=")[-1]
            good = mode != "checksum" and actual == str(zlib.crc32(data))
            if route == "complete" and good: server.completed = True
            self.reply(204 if good else 422)
        elif route in ("cancel", "keepalive"): self.reply(204)
        else: self.reply(404)

def check():
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect(("192.0.2.1", 9))
    ip = probe.getsockname()[0]
    probe.close()
    for mode in ("success", "zero", "traversal", "overflow", "disk", "truncated", "checksum", "closed", "redirect", "tampered", "stranger", "declined"):
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
