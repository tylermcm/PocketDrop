"""Independently decode QR codes in the actual desktop renders, including compact/high-DPI layouts."""
import json
from pathlib import Path
import cv2

renders = Path("build/renders")
for name in ("default", "laptop", "minimum", "resize-minimum", "wide", "laptop-150", "laptop-200"):
    img = cv2.imread(str(renders / (name + ".png")))
    text, _, _ = cv2.QRCodeDetector().detectAndDecode(img)
    if not text and hasattr(cv2, "QRCodeDetectorAruco"):
        text, _, _ = cv2.QRCodeDetectorAruco().detectAndDecode(img)
    assert text.startswith("http://"), f"QR could not be decoded: {name}"
    print(f"{name}: desktop QR decodes")

model = json.loads((renders / "model.json").read_text(encoding="utf-8"))
assert isinstance(model["rows"], list) and isinstance(model["menu"]["addrs"], list)
pc = json.loads((renders / "pc-model.json").read_text(encoding="utf-8"))
assert pc["pc"]["mode"] == 2 and pc["card"] == "pc" and "sender" in pc["pc"]["error"].lower()
print("Host model JSON parses and retains sharing/menu state")
