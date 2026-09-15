"""gen <dir>: add a real image to the test tree.  check <srcroot>: exercise the running server_test."""
import http.client, io, json, os, sys, time, urllib.parse, zipfile

OUT = os.path.join("build", "server_test_out")
INBOX = os.path.join(OUT, "inbox")
UA = {"User-Agent": "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0)"}
PORT = int(os.environ.get("PD_TEST_PORT", "47291"))
fails = 0


def expect(name, cond, detail=""):
    global fails
    print(f"{'OK  ' if cond else 'FAIL'} {name} {detail}")
    fails += 0 if cond else 1


def conn():
    return http.client.HTTPConnection("127.0.0.1", PORT, timeout=30)


def req(c, path, method="GET", headers=None, body=None):
    h = dict(UA)
    h.update(headers or {})
    c.request(method, path, body=body, headers=h)
    r = c.getresponse()
    return r, r.read()


def js(body):
    try:
        return json.loads(body)
    except ValueError:
        return {}


def token():
    with open(os.path.join(OUT, "token.txt")) as f:
        return f.read().strip()


def flag(name, on):
    p = os.path.join(OUT, name)
    if on:
        open(p, "w").close()
    elif os.path.exists(p):
        os.remove(p)
    time.sleep(0.35)  # server_test polls flags every 100 ms


def check_downloads(c, S, src):
    r, body = req(c, S)
    expect("page", r.status == 200 and b"PocketDrop" in body, f"{len(body)} bytes")
    expect("page CSP", "default-src 'none'" in (r.getheader("Content-Security-Policy") or ""))
    expect("iOS save workflow", b"Save to Files" in body and b"Save Image or Save Video" in body)
    expect("send card", b'id="pick"' in body and b"Send to" in body)

    for _ in range(100):
        r, body = req(c, S + "api/manifest")
        m = js(body)
        if m.get("ready"):
            break
        time.sleep(0.1)
    expect("manifest ready", m.get("ready"), f"files={len(m.get('files', []))} zip={m.get('zip')}")
    expect("texts", m.get("texts") == ["hello from the pc\nline two", "https://example.com/a?b=1"])

    by_path = {f["path"]: f for f in m["files"]}
    f = by_path["single.csv"]
    raw = open(os.path.join(src, "single.csv"), "rb").read()
    r, body = req(c, f"{S}f/{f['i']}/single.csv?dl=1")
    expect("file download", r.status == 200 and body == raw)
    expect("attachment header", (r.getheader("Content-Disposition") or "").startswith("attachment"))
    r, body = req(c, f"{S}f/{f['i']}/x", headers={"Range": "bytes=10-99"})
    expect("range 206", r.status == 206 and body == raw[10:100], r.getheader("Content-Range"))
    r, body = req(c, f"{S}f/{f['i']}/x", headers={"Range": "bytes=-50"})
    expect("suffix range", r.status == 206 and body == raw[-50:])
    r, body = req(c, f"{S}f/{f['i']}/x", headers={"Range": f"bytes={len(raw) + 5}-"})
    expect("416", r.status == 416)
    r, body = req(c, f"{S}f/99999/x")
    expect("bad index 404", r.status == 404)

    r, body = req(c, f"{S}zip/{m['zip']['name']}", "HEAD")
    expect("zip HEAD length", int(r.getheader("Content-Length")) == m["zip"]["size"])
    r, full = req(c, f"{S}zip/{m['zip']['name']}")
    expect("zip size", len(full) == m["zip"]["size"], f"{len(full)} bytes")
    with zipfile.ZipFile(io.BytesIO(full)) as z:
        expect("zip crc", z.testzip() is None)
        ok = all(z.read(i) == open(os.path.join(src, i.filename), "rb").read() for i in z.infolist() if not i.is_dir())
        expect("zip contents", ok, f"{len(z.infolist())} entries")
    a, b = 1000, min(len(full) - 1, 2_500_000)
    r, part = req(c, f"{S}zip/x.zip", headers={"Range": f"bytes={a}-{b}"})
    expect("zip range", r.status == 206 and part == full[a:b + 1])

    img = by_path.get("Photos Trip/photo.png")
    if img:
        r, body = req(c, f"{S}t/{img['i']}")
        good = r.status == 200 and body[:2] == b"\xff\xd8"
        if not good and sys.platform == "darwin":
            print("WARN thumbnail jpeg (Quick Look may be unavailable on headless macOS runners)")
        else:
            expect("thumbnail jpeg", good, f"{len(body)} bytes")
    r, body = req(c, f"{S}t/{by_path['single.csv']['i']}")
    expect("no thumb for csv", r.status == 404)


def check_uploads(c, S):
    def start(name, size):
        r, b = req(c, f"{S}up?name={urllib.parse.quote(name)}&size={size}", "POST")
        return r.status, js(b)

    def put(uid, offset, data):
        r, b = req(c, f"{S}up/{uid}?offset={offset}", "PUT", {"Content-Type": "application/octet-stream"}, data)
        return r.status, js(b)

    def saved(name):
        p = os.path.join(INBOX, name)
        return open(p, "rb").read() if os.path.exists(p) else None

    small = os.urandom(100_000)
    st, j = start("hello.bin", len(small))
    expect("upload start", st == 200 and j.get("id") and j.get("chunk") == 8 * 1024 * 1024, j)
    st, j2 = put(j["id"], 0, small)
    expect("single-chunk upload", st == 200 and j2.get("done") and j2.get("name") == "hello.bin", j2)
    expect("saved bytes match", saved("hello.bin") == small)

    CH = 8 * 1024 * 1024
    big = os.urandom(2 * CH + 123_457)
    st, j = start("video.mov", len(big))
    uid = j.get("id")
    st, a = put(uid, 0, big[:CH])
    expect("chunk 1", st == 200 and a.get("received") == CH, a)
    st, a = put(uid, 2 * CH, big[2 * CH:])
    expect("gap rejected with 409", st == 409 and a.get("received") == CH, a)
    st, a = put(uid, 0, big[:CH])
    expect("retrying a chunk is safe", st == 200 and a.get("received") == CH, a)
    r, b = req(c, f"{S}up/{uid}")
    expect("resume status", r.status == 200 and js(b).get("received") == CH, b)
    st, a = put(uid, CH, big[CH:2 * CH])
    expect("chunk 2", st == 200 and a.get("received") == 2 * CH, a)
    st, a = put(uid, 2 * CH, big[2 * CH:])
    expect("last chunk finishes", st == 200 and a.get("done") and a.get("received") == len(big), a)
    expect("large file intact", saved("video.mov") == big)
    expect("no part files left", not any(n.endswith(".pdpart") for n in os.listdir(INBOX)))

    st, j = start("hello.bin", 3)
    st, a = put(j["id"], 0, b"abc")
    expect("duplicate names get a suffix", a.get("name") == "hello (2).bin" and saved("hello (2).bin") == b"abc", a)

    st, j = start("../../evil:name?.txt", 4)
    st, a = put(j["id"], 0, b"evil")
    expect("unsafe names are sanitized", a.get("name") == "evil_name_.txt" and saved("evil_name_.txt") == b"evil", a)
    expect("nothing written outside the inbox", not os.path.exists(os.path.join(OUT, "..", "evil_name_.txt")))

    st, j = start("empty.txt", 0)
    expect("empty file saved immediately", st == 200 and j.get("done") and saved("empty.txt") == b"", j)

    st, j = start("cancel.bin", 1000)
    put(j["id"], 0, b"x" * 500)
    r, _ = req(c, f"{S}up/{j['id']}", "DELETE")
    expect("cancel", r.status == 204)
    expect("cancel removes the partial file", not any(n.startswith("cancel.bin") for n in os.listdir(INBOX)))
    r, _ = req(c, f"{S}up/{j['id']}")
    expect("cancelled upload is gone", r.status == 404)

    r, _ = req(c, f"{S}text", "POST", {"Content-Type": "text/plain;charset=utf-8"}, "note from phone ✓".encode())
    expect("text received", r.status == 204)
    try:
        r, _ = req(c, f"{S}text", "POST", {"Content-Type": "text/plain"}, b"x" * (1024 * 1024 + 1))
        rejected = r.status == 413
    except (ConnectionError, http.client.HTTPException):
        rejected = True
    expect("oversized text rejected", rejected)
    c = conn()  # the server closes the connection after a 413

    flag("diskfull", True)
    st, j = start("full.bin", 10)
    expect("disk full reported at start", st == 507 and j.get("error") == "disk", j)
    flag("diskfull", False)
    st, j = start("later-full.bin", 1000)
    flag("diskfull", True)
    st, a = put(j.get("id"), 0, b"y" * 1000)
    expect("disk full reported mid-upload", st == 507 and a.get("error") == "disk", a)
    flag("diskfull", False)
    expect("failed upload leaves no partial file", not any(n.startswith("later-full.bin") for n in os.listdir(INBOX)))

    time.sleep(0.4)
    log = open(os.path.join(OUT, "received.log"), encoding="utf-8").read()
    expect("desktop was told about arrivals",
           "file\tiPhone\thello.bin" in log and "file\tiPhone\tvideo.mov" in log and "text\tiPhone\t" in log,
           f"{log.count(chr(10))} items")
    return c


def check_expiry(c, S):
    t0 = token()
    flag("unshare", True)
    stable = True
    until = time.time() + 5
    while time.time() < until:
        req(c, S + "api/manifest")
        stable &= token() == t0
        time.sleep(0.3)
    expect("an open phone page pauses link rotation", stable)

    time.sleep(4.5)
    t1 = token()
    expect("unused link rotates while nothing is shared", t1 != t0)
    r, _ = req(c, S + "api/manifest")
    expect("open session survives rotation", r.status == 200)
    r, _ = req(c, "/" + t0 + "/")
    expect("old link stops working", r.status == 404)

    flag("unshare", False)
    t1 = token()
    time.sleep(3.5)
    expect("shared link skips the short rotation", token() == t1)
    time.sleep(3.5)
    expect("shared link expires after the idle timeout", token() != t1)
    r, _ = req(c, S + "api/manifest")
    expect("idle session expires", r.status == 404)


def check(src):
    try:
        c = conn()
        r, _ = req(c, "/ping")
        expect("ping 204", r.status == 204)
        r, _ = req(c, "/nope/")
        expect("bad token 404", r.status == 404)
        tok = token()
        r, _ = req(c, "/" + tok + "1234/")
        expect("longer token 404", r.status == 404)
        r, _ = req(c, "/" + tok + "/")
        S = r.getheader("Location") or ""
        expect("QR link opens a phone session", r.status == 302 and S.startswith("/s/") and S.endswith("/"), S)
        r, _ = req(c, S[:-1])
        expect("session slash redirect", r.status == 301 and r.getheader("Location") == S)
        r, _ = req(c, "/s/notasession/api/manifest")
        expect("unknown session 404", r.status == 404)

        check_downloads(c, S, src)
        c = check_uploads(c, S)
        check_expiry(c, S)
    finally:
        flag("done", True)
    print("ALL OK" if not fails else f"{fails} FAILURES")
    return 1 if fails else 0


if __name__ == "__main__":
    if sys.argv[1] == "gen":
        from PIL import Image, ImageDraw
        im = Image.new("RGB", (1200, 800), (40, 90, 160))
        ImageDraw.Draw(im).ellipse((300, 150, 900, 650), fill=(250, 200, 60))
        im.save(os.path.join(sys.argv[2], "Photos Trip", "photo.png"))
        sys.exit(0)
    sys.exit(check(sys.argv[2]))
