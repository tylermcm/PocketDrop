"""gen <dir>: add a real image to the test tree.  check <srcroot>: exercise the running server_test."""
import http.client, io, json, os, sys, time, zipfile

T = "/testtoken123"
fails = 0


def expect(name, cond, detail=""):
    global fails
    print(f"{'OK  ' if cond else 'FAIL'} {name} {detail}")
    fails += 0 if cond else 1


def req(conn, path, method="GET", headers=None):
    conn.request(method, path, headers=headers or {"User-Agent": "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0)"})
    r = conn.getresponse()
    return r, r.read()


def check(src):
    c = http.client.HTTPConnection("127.0.0.1", 47291, timeout=20)
    r, _ = req(c, "/ping"); expect("ping 204", r.status == 204)
    r, _ = req(c, "/nope/"); expect("bad token 404", r.status == 404)
    r, _ = req(c, "/testtoken1234/"); expect("longer token 404", r.status == 404)
    r, _ = req(c, T); expect("redirect", r.status == 301 and r.getheader("Location") == T + "/")
    r, body = req(c, T + "/"); expect("page", r.status == 200 and b"PocketDrop" in body, f"{len(body)} bytes")
    expect("page CSP", "default-src 'none'" in (r.getheader("Content-Security-Policy") or ""))
    expect("iOS save workflow", b"Save to Files" in body and b"Save Image or Save Video" in body)

    for _ in range(100):
        r, body = req(c, T + "/api/manifest")
        m = json.loads(body)
        if m["ready"]:
            break
        time.sleep(0.1)
    expect("manifest ready", m["ready"], f"files={len(m['files'])} zip={m['zip']}")
    expect("texts", m["texts"] == ["hello from the pc\nline two", "https://example.com/a?b=1"])

    by_path = {f["path"]: f for f in m["files"]}
    f = by_path["single.csv"]
    raw = open(os.path.join(src, "single.csv"), "rb").read()
    r, body = req(c, f"{T}/f/{f['i']}/single.csv?dl=1")
    expect("file download", r.status == 200 and body == raw)
    expect("attachment header", (r.getheader("Content-Disposition") or "").startswith("attachment"))
    hdr = {"Range": "bytes=10-99"}
    r, body = req(c, f"{T}/f/{f['i']}/x", headers=hdr)
    expect("range 206", r.status == 206 and body == raw[10:100], r.getheader("Content-Range"))
    r, body = req(c, f"{T}/f/{f['i']}/x", headers={"Range": "bytes=-50"})
    expect("suffix range", r.status == 206 and body == raw[-50:])
    r, body = req(c, f"{T}/f/{f['i']}/x", headers={"Range": f"bytes={len(raw) + 5}-"})
    expect("416", r.status == 416)
    r, body = req(c, f"{T}/f/99999/x"); expect("bad index 404", r.status == 404)

    r, body = req(c, f"{T}/zip/{m['zip']['name']}", "HEAD")
    expect("zip HEAD length", int(r.getheader("Content-Length")) == m["zip"]["size"])
    t0 = time.time()
    r, full = req(c, f"{T}/zip/{m['zip']['name']}")
    dt = time.time() - t0
    expect("zip size", len(full) == m["zip"]["size"], f"{len(full)} bytes in {dt*1000:.0f} ms")
    with zipfile.ZipFile(io.BytesIO(full)) as z:
        expect("zip crc", z.testzip() is None)
        ok = all(z.read(i) == open(os.path.join(src, i.filename), "rb").read() for i in z.infolist() if not i.is_dir())
        expect("zip contents", ok, f"{len(z.infolist())} entries")
    a, b = 1000, min(len(full) - 1, 2_500_000)
    r, part = req(c, f"{T}/zip/x.zip", headers={"Range": f"bytes={a}-{b}"})
    expect("zip range", r.status == 206 and part == full[a:b + 1])

    img = by_path.get("Photos Trip/photo.png")
    if img:
        r, body = req(c, f"{T}/t/{img['i']}")
        good = r.status == 200 and body[:2] == b"\xff\xd8"
        if not good and sys.platform == "darwin":
            print("WARN thumbnail jpeg (Quick Look may be unavailable on headless macOS runners)")
        else:
            expect("thumbnail jpeg", good, f"{len(body)} bytes")
    r, body = req(c, f"{T}/t/{by_path['single.csv']['i']}")
    expect("no thumb for csv", r.status == 404)
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
