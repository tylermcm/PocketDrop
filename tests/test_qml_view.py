"""The shared QML view: it loads in every scripted state without QML errors, and its controls send the commands the engine expects.

Needs PySide6 (skipped without it). Run with QT_QPA_PLATFORM=offscreen.
"""
import os
import sys
from pathlib import Path

import pytest

pytest.importorskip("PySide6")
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from PySide6.QtCore import QObject, QPointF, Qt, qInstallMessageHandler  # noqa: E402
from PySide6.QtQuick import QQuickItem  # noqa: E402
from PySide6.QtTest import QTest  # noqa: E402

import preview_qml as pq  # noqa: E402

MESSAGES: list[str] = []


@pytest.fixture(scope="module")
def preview():
    qInstallMessageHandler(lambda mode, ctx, text: MESSAGES.append(text))
    p = pq.Preview(400, 720)
    yield p


def click(preview, name):
    item = preview.window.findChild(QQuickItem, name)
    assert item is not None, f"no item named {name}"
    assert item.isVisible(), f"{name} is not visible"
    point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2)).toPoint()
    QTest.mouseClick(preview.window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
    preview.settle(0.15)


@pytest.mark.parametrize("name", list(pq.STATES))
def test_every_state_renders_without_qml_errors(preview, name):
    MESSAGES.clear()
    preview.show(name)
    image = preview.grab()
    assert not image.isNull() and image.width() >= 400
    problems = [m for m in MESSAGES if any(k in m for k in ("TypeError", "ReferenceError", "is not a type", "Cannot ", "polish() loop", "Binding loop"))]
    assert not problems, problems[:3]


def test_a_narrow_short_window_still_lays_out(preview):
    MESSAGES.clear()
    narrow = pq.Preview(340, 520)
    narrow.show("wifi_files")
    narrow.grab()
    problems = [m for m in MESSAGES if any(k in m for k in ("TypeError", "polish() loop", "Binding loop"))]
    assert not problems, problems[:3]


def test_copy_link_sends_copy(preview):
    preview.show("wifi_empty")
    preview.backend.sent.clear()
    click(preview, "copyChip")
    assert ("copy", "") in preview.backend.sent


def test_the_switch_turns_anywhere_on_and_off(preview):
    preview.show("wifi_empty")
    preview.backend.sent.clear()
    click(preview, "modeSwitch")
    assert ("mode", "1") in preview.backend.sent
    preview.show("anywhere")
    preview.backend.sent.clear()
    click(preview, "modeSwitch")
    assert ("mode", "0") in preview.backend.sent


def test_the_pairing_prompt_allows_or_declines(preview):
    preview.show("pc_send_pairing")
    preview.backend.sent.clear()
    click(preview, "allowButton")
    assert ("pc_allow", "") in preview.backend.sent
    preview.show("pc_send_pairing")
    preview.backend.sent.clear()
    click(preview, "declineButton")
    assert ("pc_decline", "") in preview.backend.sent


def test_the_receive_code_is_hidden_on_the_local_network_until_asked_for(preview):
    preview.show("wifi_empty")
    assert preview.window.findChild(QQuickItem, "codeHint").isVisible()
    assert not preview.window.findChild(QQuickItem, "codeRow").isVisible()
    click(preview, "codeHint")
    assert preview.window.findChild(QQuickItem, "codeRow").isVisible()
    preview.show("anywhere")                                  # a link that may be forwarded always shows its code
    assert preview.window.findChild(QQuickItem, "codeRow").isVisible()


def test_the_risk_prompt_answers_through_the_backend(preview):
    preview.show("risk")
    preview.backend.sent.clear()
    preview.backend.confirmRisk(True, False)
    assert ("confirmRisk", "True,False") in preview.backend.sent
