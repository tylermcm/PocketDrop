"""Render qml/PocketDropView.qml in scripted states to PNG files (needs PySide6; no PocketDrop engine involved).

    QT_QPA_PLATFORM=offscreen python tools/preview_qml.py OUT_DIR [width height]

The same scripted states back tests/test_qml_view.py. Each state is shaped like Ui::stateJson().
"""
from __future__ import annotations

import copy
import sys
from pathlib import Path

from PySide6.QtCore import Property, QMetaObject, QObject, QUrl, Signal, Slot
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickWindow, QSGRendererInterface

QML = Path(__file__).resolve().parents[1] / "qml"

QR_PATH = "M0 0h7v1h-7zM8 0h1v1h-1z"          # a stand-in; the real one is the engine's module path


def qr_path(n: int = 29) -> str:
    """A believable-looking QR pattern: three finders and pseudo-random modules."""
    import random

    rnd = random.Random(7)
    parts = []
    for y in range(n):
        for x in range(n):
            finder = (x < 8 and y < 8) or (x >= n - 8 and y < 8) or (x < 8 and y >= n - 8)
            if finder:
                inner = x in (0, 6, n - 7, n - 1) or y in (0, 6, n - 7, n - 1) or (2 <= (x % (n - 7 if x >= n - 8 else 7)) <= 4 and 2 <= (y % (n - 7 if y >= n - 8 else 7)) <= 4)
                dark = inner and not (x == 7 or y == 7 or x == n - 8 or y == n - 8)
            else:
                dark = rnd.random() < 0.5
            if dark:
                parts.append(f"M{x} {y}h1v1h-1z")
    return "".join(parts)


BASE = {
    "mode": 0, "offline": True, "status": "Ready", "tone": "ok", "card": "qr", "cardMsg": "", "pct": 0, "btHotspot": False,
    "badges": ["ok", "", ""], "qrSize": 29, "qrPath": qr_path(), "hasLink": True, "canOpen": True,
    "linkText": "192.168.1.24:47291/k_0fSDWUG_YS5ZN8vPvwy", "linkEmpty": "", "copied": False, "copyLabel": "Copy link",
    "info": {"kind": "code", "left": "", "right": "", "code": "ZRQ5-DMYC", "frac": 0.0, "incoming": False},
    "rows": [], "sharing": False,
    "pc": {"mode": 0, "sent": False, "phase": 0, "senders": [], "code": "", "pair": {"pending": False, "name": "", "code": ""}, "name": "", "error": "", "received": 0, "total": 0},
    "menu": {"burn": False, "auto": False, "warn": True, "topmost": False, "tunnelOpen": False, "inbox": "Downloads/PocketDrop",
             "addrs": [{"ip": "192.168.1.24", "adapter": "Wi-Fi", "selected": True}], "noteClip": False, "phones": 2,
             "history": [{"i": 0, "kind": "f", "title": "IMG_4821.HEIC", "device": "iPhone", "when": "3 min ago", "size": 3400000},
                         {"i": 1, "kind": "n", "title": "Note: Wi-Fi at the flat", "device": "iPhone", "when": "1 h ago", "size": 28},
                         {"i": 2, "kind": "s", "title": "Georgia trip.zip", "device": "Pixel 9", "when": "2 d ago", "size": 1}]},
    "busy": False, "recent": False,
}

ROWS = [
    {"index": 0, "key": "e0", "kind": "file", "title": "DSC_4821.NEF", "sub": "42.7 MB", "path": "", "tone": "", "thumbs": []},
    {"index": 1, "key": "e1", "kind": "folder", "title": "Georgia trip", "sub": "212 files · 3.1 GB", "path": "", "tone": "", "thumbs": []},
    {"index": 2, "key": "e2", "kind": "note", "title": "Wi-Fi at the flat is gz-guest", "sub": "Note · 28 characters", "path": "", "tone": "", "thumbs": []},
]
RECEIVED = [{"index": 3, "key": "e3", "kind": "file", "title": "IMG_2290.HEIC", "sub": "Received from iPhone · 3.4 MB", "path": "x", "tone": "recv", "thumbs": []}]


def make(**changes) -> dict:
    state = copy.deepcopy(BASE)
    for key, value in changes.items():
        if isinstance(value, dict) and isinstance(state.get(key), dict):
            state[key].update(value)
        else:
            state[key] = value
    return state


STATES = {
    "wifi_empty": (make(), [], {}),
    "wifi_files": (make(info={"kind": "sum", "left": "2 items  ·  3.1 GB", "right": "zip 2.7 GB · compressed"}, sharing=True), ROWS + RECEIVED, {}),
    "wifi_sending": (make(status="Sending", tone="send", info={"kind": "active", "left": "62%  ·  DSC_4821.NEF  →  Pixel 8", "right": "18.3 MB/s", "frac": 0.62}), ROWS, {}),
    "anywhere": (make(mode=1, linkText="quiet-river-lamp.trycloudflare.com/k_0fSDW", copyLabel="Copy QR code", menu={"tunnelOpen": True}), ROWS[:1], {}),
    "anywhere_missing": (make(mode=1, card="missing", hasLink=False, linkEmpty="Link appears when ready", status="Needs setup", tone="warn", info={"kind": "hint", "left": "Anywhere needs a one-time download."}), [], {}),
    "offline_hotspot": (make(mode=2, linkText="192.168.137.1:47291/k_0fSD", info={"kind": "hint", "left": "Join Wi-Fi PocketDrop-4F2A · password 7k2m9xqd, then scan."}), [], {}),
    "no_network": (make(card="nonet", hasLink=False, linkEmpty="Link appears when ready", status="No network", tone="warn", info={"kind": "hint", "left": "Connect to Wi-Fi, or start an offline hotspot."}), [], {}),
    "pc_send_waiting": (make(card="pc", status="PC Send", pc={"mode": 1}), ROWS[:1], {}),
    "pc_send_pairing": (make(card="pc", status="Confirm code", pc={"mode": 1, "pair": {"pending": True, "name": "KITCHEN-PC", "code": "483 291"}}), ROWS[:1], {}),
    "pc_receive_pairing": (make(card="pc", status="Confirm code", pc={"mode": 2, "phase": 7, "code": "483 291"}), [], {}),
    "pc_receive_progress": (make(card="pc", status="Receiving", tone="recv", pc={"mode": 2, "phase": 2, "received": 640, "total": 1000, "name": "Georgia trip.zip"},
                                 info={"kind": "active", "left": "64%  ·  Georgia trip.zip  ←  KITCHEN-PC", "right": "92 MB/s", "frac": 0.64, "incoming": True}), [], {}),
    "pc_pick_sender": (make(card="pc", status="PC Receive", pc={"mode": 2, "phase": 0, "senders": [{"name": "KITCHEN-PC", "ip": "192.168.1.31"}, {"name": "STUDY-LAPTOP", "ip": "192.168.1.40"}]}), [], {}),
    "menu_open": (make(), ROWS, {"menu": True}),
    "risk": (make(), [], {"risk": True}),
}


class Backend(QObject):
    stateChanged = Signal()
    rowsChanged = Signal()
    riskChanged = Signal()

    def __init__(self, state: dict, rows: list, risk: bool = False):
        super().__init__()
        self._st, self._rows, self._risk = state, rows, risk
        self.sent: list[tuple[str, str]] = []

    st = Property("QVariantMap", lambda self: self._st, notify=stateChanged)
    rows = Property("QVariantList", lambda self: self._rows, notify=rowsChanged)
    riskPending = Property(bool, lambda self: self._risk, notify=riskChanged)

    def setState(self, state: dict, rows: list, risk: bool = False) -> None:
        self._st, self._rows, self._risk = state, rows, risk
        self.stateChanged.emit()
        self.rowsChanged.emit()
        self.riskChanged.emit()

    @Slot()
    def start(self): pass

    @Slot(str)
    @Slot(str, str)
    def command(self, name, arg=""): self.sent.append((name, arg))

    @Slot(int)
    def setMode(self, mode): self.sent.append(("mode", str(mode)))

    @Slot("QVariantList")
    def addUrls(self, urls): self.sent.append(("addUrls", str(len(urls))))

    @Slot(str)
    def addText(self, text): self.sent.append(("addText", text))

    @Slot(str)
    def removeRow(self, key): self.sent.append(("removeRow", key))

    @Slot()
    def clearAll(self): self.sent.append(("clearAll", ""))

    @Slot(QUrl)
    def changeInbox(self, url): self.sent.append(("changeInbox", url.toString()))

    @Slot(bool, bool)
    def confirmRisk(self, proceed, dismiss): self.sent.append(("confirmRisk", f"{proceed},{dismiss}"))


class Preview:
    """The view in a window of the given size, backed by `Backend`."""

    def __init__(self, width: int = 400, height: int = 720, dark: bool = True):
        QQuickWindow.setGraphicsApi(QSGRendererInterface.GraphicsApi.Software)
        self.app = QGuiApplication.instance() or QGuiApplication(sys.argv[:1])
        self.backend = Backend(make(), [])
        self.engine = QQmlApplicationEngine()
        import json
        from PySide6.QtGui import QColor
        from PySide6.QtQml import QQmlPropertyMap
        palette = json.loads((QML / "theme.json").read_text())["dark" if dark else "light"]
        self.theme = QQmlPropertyMap()
        for key, value in palette.items():
            self.theme.insert(key, QColor(value))
        ctx = self.engine.rootContext()
        ctx.setContextProperty("theme", self.theme)
        ctx.setContextProperty("pocketDrop", self.backend)
        self.engine.loadData(f"""
            import QtQuick
            import QtQuick.Window
            Window {{ width: {width}; height: {height}; visible: true; color: theme.base
                function openMenu() {{ view.menuPopup.open() }}
                function closeMenu() {{ view.menuPopup.close() }}
                PocketDropView {{ id: view; anchors.fill: parent }} }}
        """.encode(), QUrl.fromLocalFile(str(QML / "host.qml")))
        if not self.engine.rootObjects():
            raise RuntimeError("the view did not load")
        self.window = self.engine.rootObjects()[0]

    def _component(self, name: str):
        from PySide6.QtQml import QQmlComponent
        component = QQmlComponent(self.engine, QUrl.fromLocalFile(str(QML / name)))
        if component.status() != QQmlComponent.Status.Ready:
            raise RuntimeError(component.errorString())
        return component

    def show(self, name: str):
        state, rows, opts = STATES[name]
        QMetaObject.invokeMethod(self.window, "closeMenu")
        self.backend.setState(state, rows, bool(opts.get("risk")))
        if opts.get("menu"):
            QMetaObject.invokeMethod(self.window, "openMenu")
        self.settle()

    def settle(self, seconds: float = 0.25):
        import time
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.app.processEvents()
            time.sleep(0.005)

    def grab(self, path: str | None = None):
        image = self.window.grabWindow()
        if path:
            image.save(path)
        return image


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "preview")
    out.mkdir(parents=True, exist_ok=True)
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    height = int(sys.argv[3]) if len(sys.argv) > 3 else 720
    preview = Preview(width, height)
    for name in STATES:
        preview.show(name)
        preview.grab(str(out / f"{name}.png"))
        print("wrote", out / f"{name}.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
