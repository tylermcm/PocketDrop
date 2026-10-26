# PocketDrop 2.2 development handoff

## Recovered Image Triage work

On 2026-10-25 the standalone checkout was compared read-only with
`C:\Users\tylle\OneDrive\Documents\Playground\native\pocketdrop`.
Its UPSTREAM file identified `1d8ae6a5f545e3ffd3d30d09bb9c9eb2360c3267` plus uncommitted changes.
The standalone starting HEAD was `fec79dd`.

Recovered differing files: core/http.cpp, http.h, platform.h, qr.cpp, qr.h,
webpage.h; ui/gfx.h, icons.cpp, shell.h, ui.cpp, ui.h; plus new
core/hotspot_stub.cpp and win/hotspot_win.cpp. Unchanged engine/platform files
were retained. The Image Triage checkout was not modified.

These restore offline hotspot and Bluetooth fallback, Wi-Fi QR payloads,
Anywhere receive-code authorization, QR clipboard support, compact link and
button content, and Ui::stateJson/command/removePaths/menuCommand used by QML.
Windows now implements the shell's image clipboard and Bluetooth setup methods;
the standalone Share action copies the link for pasting into another app.

Before re-vendoring into Image Triage, update its native DLL build to include
core/nearby.cpp and core/pc_transfer.cpp. Its sync script already copies the whole core directory, but
its current build_windows.bat enumerates sources explicitly. Retain its capi
adapter and QML model. No DLL or QML host was changed or rebuilt in this task.

## New behavior

- Windows uses a normal resizable frame and bounds startup, DPI changes and
  display/work-area changes to the monitor. The normal size is saved in DIPs.
  macOS/GTK hosts also permit resizing and bound their initial size.
- Compact non-QR status cards keep action buttons inside short windows.
- Native PC Share replaces the initial browser/code workflow. Send snapshots the
  prepared bundle into a separate temporary PC offer on the existing HTTP server.
  Receive discovers waiting offers, connects automatically to a single sender,
  or presents a name picker when there are several. No browser or code entry.
  Top tabs switch to Send / Receive / Phone. PC offers never use the phone token.
  A receiver that disappears after streaming releases its claim after 30 seconds;
  a live receiver keeps the claim alive while checking a large file.
- Files stream directly to a temporary file in the receiver's inbox, with speed,
  progress and cancellation. One file remains loose; folders/multiple files use
  the existing streamed ZIP, retaining empty directories and Unicode names.
  The receiver verifies length and CRC before atomically saving without replacing
  any existing destination. A receipt closes the sender's offer. Returning to
  Phone, stopping Send, changing the selection or shutting down revokes it.
- Discovery protocol v2 carries an explicit, temporary offer only during Send.
  Receive sends a discovery query; senders reply and withdraw on completion/stop.
  The PC endpoints reject cloudflared's loopback access. Native clients bypass
  proxies and refuse redirects. This is explicit LAN sharing: any local PC can
  receive an offered transfer, one receiver at a time.
- Windows has a manual update check in the menu. It reads GitHub stable-release
  metadata, compares numeric versions, restricts the installer to this repo's
  expected release-asset URL, verifies the API-provided SHA-256 digest, then
  launches the installer after the user's confirmation. No update is installed
  silently. Releases lacking an installer/digest open the release page instead.

## Build and publish

1. Update src/core/version.h (`PD_VERSION` and `PD_VERSION_NUM`).
2. Run build.bat and the regression checks. Run build_installer.bat with Inno
   Setup 6 installed, or set ISCC to a portable compiler. The portable EXE
   remains available alongside the installer.
3. Publish only after reviewing the resulting installer and release assets.
   CI builds portable Windows, Windows Setup, macOS ZIP and Linux tarball.
   A pushed `vMAJOR.MINOR.PATCH` tag prepares a **draft** release after all jobs
   pass. Review and publish it to make it visible to the in-app updater.
4. Keep the Windows asset named `PocketDrop-Setup.exe`. GitHub supplies its
   SHA-256 digest in release metadata; without that digest the app refuses
   automatic installer execution. Verify the tag matches the compiled version.

The installer uses a stable AppId and `%LOCALAPPDATA%\Programs\PocketDrop`,
requires no elevation, and retains settings and received files on uninstall.
It is currently unsigned; no signing certificate was configured.
The Windows build compiles QR with local `/O2` optimization and excludes that
translation unit from `/GL`: VS 2022 14.40 otherwise hit an internal compiler
error in qr::wifi during the final release link. The remaining sources retain
whole-program optimization.

## Local verification

- Core QR decoding, independent zlib decompression, ZIP CRC/content checks.
- Existing HTTP suite: downloads, byte ranges, thumbnails, chunk retry/resume,
  cancellation, disk-full paths, filename sanitization and link/session expiry.
- Real native sender/receiver harness: single/ZIP/empty files, destination
  collisions, one receiver at a time, checksum rejection, cancellation/retry,
  sender revocation and unchanged phone tokens.
- Adversarial native-client fixtures: path traversal, numeric overflow, disk
  capacity preflight, truncated body, checksum mismatch, sender loss and refused
  redirects. Independent Python ZIP CRC/content checks cover folder structure,
  Unicode names and empty directories.
- Desktop harness: window geometry, numeric versions, discovery packet parsing,
  and actual software Direct2D renders at compact, default, wide, 150% and 200% DPI
  sizes, plus compact enable/error/busy cards. PNGs go to build/renders.
  Independent OpenCV decoding also verifies QR usability in those renders.
  Native PC waiting/progress/completed/error states are rendered at three sizes,
  and model-level checks cover automatic single-sender connection and the
  multiple-sender picker.
- Update harness: stable/draft/prerelease parsing, malformed metadata, official
  asset URL restriction, known SHA-256 fixture, corruption and cancellation.
- UDP harness: receipt and parsing on a separate test port, sender IP,
  malformed advertisement rejection and bounded shutdown.

Commands: tests/build_tests.bat build/tests, tests/build_desktop_test.bat,
build/tests/desktop_test.exe, build/tests/update_test.exe,
build/tests/discovery_test.exe, build/tests/pc_transfer_test.exe,
python tests/nearby_check.py, python tests/check_renders.py.
The original README/CI describe core and HTTP fixture commands.

Outstanding physical checks: two separate PCs on the same LAN (including their
firewall prompts), actual phone hotspot/Bluetooth behavior, clean install and
upgrade on a separate Windows user/VM, a real newer published release update,
and macOS/Linux builds. Local browser/desktop automation initialization failed;
Direct2D images were generated by the app's own renderer and inspected instead.
No GitHub publication or installation into the user's normal app directory was
performed.

Local 2.2.0 validation passed on 2026-10-25: release EXE and Setup build, native
PC integration and adversarial fixtures, sender selection, UDP offer withdrawal,
24 Direct2D renders, independent QR decoding, core compression/ZIP tests, the
full phone HTTP/upload/session suite and updater validation. The physical checks
listed above remain outstanding.

Local 2.2.1 resize fix: preserve the 400 x 700 DIP default, lower the outer
minimum to 320 x 380 DIPs (also on restore), and extend native side/bottom resize
edges into the empty client margins. The desktop harness now exercises the
production frame handlers on an isolated hidden HWND: minimum tracking size,
shrink/grow and edge/corner hit tests. Compact phone/PC renders include the
304 x 341 client area. Native frame tests, renders and independent QR checks
passed; portable EXE and Setup rebuilt. Interactive mouse dragging remains
unverified because the Computer Use runtime failed to initialize.

The user's open process was the old 1.0.0 executable at
`C:\Users\tylle\Downloads\Installers\PocketDrop.exe`, started before the new
build. It must be closed before launching the updated executable: single-instance
forwarding otherwise foregrounds that old process. The old binary and running
process were left untouched.

## Suggested next features

A persistent transfer history with retry/reveal actions and an optional receiver
folder picker would help longer sessions. Native sending/receiving is implemented;
trusted-device setup and codes are not required for the normal LAN workflow.
