"""gen <dir>: create zip test inputs.  check <outdir> <srcroot>: validate core_test output."""
import os, random, sys, zipfile, zlib


def gen(d):
    random.seed(7)
    os.makedirs(f"{d}/Photos Trip/nested/deeper", exist_ok=True)
    os.makedirs(f"{d}/Photos Trip/empty dir", exist_ok=True)
    with open(f"{d}/Photos Trip/notes.txt", "w", encoding="utf-8") as f:
        f.write("Café ünïcode naïve 日本語\n" * 5000)
    with open(f"{d}/Photos Trip/nested/random.bin", "wb") as f:
        f.write(os.urandom(3_000_000))
    with open(f"{d}/Photos Trip/nested/fake.jpg", "wb") as f:
        f.write(b"\xff\xd8" + b"A" * 100_000)
    with open(f"{d}/Photos Trip/nested/deeper/data.unknownext", "wb") as f:
        f.write(bytes(i % 251 for i in range(400_000)))
    with open(f"{d}/Photos Trip/nested/tiny.txt", "wb") as f:
        f.write(b"hi")
    with open(f"{d}/Photos Trip/nested/zero.dat", "wb"):
        pass
    with open(f"{d}/single.csv", "w") as f:
        f.write("\n".join(f"{i},{random.random()}" for i in range(50000)))


def check(out, src):
    import cv2
    ok = True
    det = cv2.QRCodeDetector()
    i = 0
    while os.path.exists(f"{out}/qr_{i}.pgm"):
        img = cv2.imread(f"{out}/qr_{i}.pgm", cv2.IMREAD_GRAYSCALE)
        exp = open(f"{out}/qr_{i}.txt", encoding="utf-8").read()
        got, _, _ = det.detectAndDecode(img)
        if got != exp and hasattr(cv2, "QRCodeDetectorAruco"):
            got, _, _ = cv2.QRCodeDetectorAruco().detectAndDecode(img)
        good = got == exp
        print(f"QR {i}: {'OK' if good else 'FAIL (got %r)' % got[:30]}")
        ok &= good
        i += 1
    i = 0
    while os.path.exists(f"{out}/deflate_{i}.raw"):
        raw = open(f"{out}/deflate_{i}.raw", "rb").read()
        comp = open(f"{out}/deflate_{i}.def", "rb").read()
        try:
            good = zlib.decompress(comp, -15) == raw
        except Exception as e:
            good = False
            print("  ", e)
        ref = len(zlib.compress(raw, 6)) - 6
        print(f"deflate {i}: {'OK' if good else 'FAIL'} ours={len(comp)} zlib6={ref}")
        ok &= good
        i += 1
    with zipfile.ZipFile(f"{out}/bundle.zip") as z:
        bad = z.testzip()
        print("zip testzip:", "OK" if bad is None else f"FAIL at {bad}")
        ok &= bad is None
        for info in z.infolist():
            if info.is_dir():
                print(f"  dir  {info.filename}")
                continue
            path = os.path.join(src, info.filename)
            same = z.read(info) == open(path, "rb").read()
            ok &= same
            method = "deflate" if info.compress_type == 8 else "store"
            print(f"  {'OK ' if same else 'BAD'} {method:7} {info.file_size:>9} -> {info.compress_size:>9} {info.filename}")
    print("ALL OK" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(gen(sys.argv[2]) if sys.argv[1] == "gen" else check(sys.argv[2], sys.argv[3]))
