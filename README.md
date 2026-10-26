# PocketDrop

Move files and text between your computer and your phone, in either direction, by scanning a QR code. No phone app, no account.

Native C++20 desktop apps for Windows, macOS, and Linux, with no phone app or account required.

## Features

- **Drop anything**: files, folders, dragged text, or **Ctrl+V** for copied files, text, or screenshots (saved as PNG).
- **Smart packaging**:
  - A single file downloads as itself.
  - Several files or any folder also get a **Download all** zip that keeps the folder structure.
  - Each file is compressed only if that pays off: already-compressed formats (JPEG, MP4, ZIP, DOCX, ...) are stored as-is, text-like formats are deflated, and unknown types are sample-tested first.
  - CRCs and compression are computed in parallel in the background, so the zip streams immediately with an exact size, resumable range requests, and zip64 for very large bundles.
- **Two ways to connect**:
  - **Same Wi-Fi**: direct LAN transfer, fastest.
  - **Anywhere**: a Cloudflare quick tunnel (`https://*.trycloudflare.com`), so a phone on mobile data can reach the computer. It needs no port forwarding, no account, and no VPN app on the phone. `cloudflared` is fetched once on request and validated before installation.
- **Send from your phone**:
  - The phone page has **Send to <computer>**: pick photos, videos or any files, or send a note.
  - Uploads go in resumable 8 MB chunks with progress, speed, cancel and retry, and keep the screen awake while sending. There's no size limit.
  - Files land in `Downloads/PocketDrop` (changeable from the â‹¯ menu) under their original name, with ` (2)` added if the name is taken. Received files and notes appear at the top of the desktop list: click a file to show it in its folder, or a note to copy it.
  - If the computer runs out of disk space, the phone shows a clear error instead of stalling.
- **Phone page**:
  - The file list shows available thumbnails for photos, videos and PDFs using each desktop platform's native image services.
  - Buttons: per-file download, **Download all**, copy (and open, for links) on shared text, and **Save to Photos** over HTTPS.
  - Updates live as you add files and adapts to light or dark mode.
- **Desktop**:
  - Resizable window that fits the monitor's work area, including laptop screens at high DPI. Windows remembers its size.
  - Live transfer progress and speed, plus a "phone connected" indicator.
  - "Stop sharing after a download", **New link** (revokes the old one), network address picker, always on top.
  - Optional **Send to â†’ PocketDrop** shortcut. The app is single-instance, so Send to adds to the open window.
- **Native PC Share**: on PC A choose **PC Share → Send**, then select or drop files. On PC B choose **PC Share → Receive**. A single waiting sender connects automatically; choose a computer by name when several are sending. Transfers show progress and speed, support cancellation, save directly to the received-files folder, and close after the receiver confirms completion. One file arrives as itself; folders or multiple files arrive as a ZIP. No browser, copied link, or code entry.
- **Offline (Windows)**: create a temporary Wi-Fi hotspot, or use Bluetooth tethering when no Wi-Fi adapter is available. Recovered from the Image Triage integration; availability depends on the adapter and driver.
- **Windows installation and updates**: choose the portable `PocketDrop.exe` or `PocketDrop-Setup.exe`. The installer supports upgrades in place. Use **â‹¯ â†’ Check for updates** to download a newer published release, verify its SHA-256 digest, and launch its installer.

## Build

### Windows

Requires Visual Studio 2022 Build Tools with the C++ workload.

```bat
build.bat
```

Output: `build\PocketDrop.exe`. You can also pass paths on the command line: `PocketDrop.exe file1 folder2`.

To create the optional per-user installer, install [Inno Setup 6](https://jrsoftware.org/isinfo.php), then run `build_installer.bat` after building the app. Set `ISCC` to the full path to `ISCC.exe` if it is in a custom location. Output: `build\PocketDrop-Setup.exe`. It offers Start menu, optional desktop and Send to shortcuts, and an uninstaller. Updates and uninstall preserve settings and received files.

The update checker uses stable GitHub releases with a `vMAJOR.MINOR.PATCH` tag and an asset named `PocketDrop-Setup.exe`. Installation requires confirmation in the app. See [release and recovery notes](docs/release-and-recovery.md) for publishing and validation details.

On first launch Windows asks whether to allow network access. Choose **Allow** for private networks, or phones on your Wi-Fi can't connect.

### macOS

Requires Xcode or the Command Line Tools. Builds a universal Apple Silicon + Intel app for macOS 11 or later.

```sh
bash build_mac.sh
```

Output: `build/PocketDrop.app` and `build/PocketDrop-mac.zip`.

### Linux

Requires GTK 3, libcurl, a C++20 compiler, and pkg-config. The release build targets x86_64 Ubuntu 22.04 or later.

```sh
sudo apt install g++ pkg-config libgtk-3-dev libcurl4-openssl-dev
bash build_linux.sh
```

Output: `build/PocketDrop-linux-x86_64/PocketDrop` and `build/PocketDrop-linux-x86_64.tar.gz`.

## Security model

- Every link has a random 128-bit token. Wrong tokens get a plain 404.
- Opening the QR link starts a private phone session at its own URL, so the QR link can change without interrupting an open page or an upload.
- Links refresh on their own:
  - a new link every time PocketDrop starts;
  - every 60 seconds while nothing is shared and no phone has the page open;
  - once something is shared, after 15 minutes with no phone activity. Phone sessions also end after 15 minutes idle, but never during a transfer.
- Links also end when PocketDrop closes, when you choose **New link**, or after a download if that option is on.
- Responses carry `Referrer-Policy: no-referrer` and a strict CSP. HTML and SVG previews are sandboxed.
- Same Wi-Fi mode is plain HTTP on your LAN. Anywhere mode is HTTPS from the phone to Cloudflare, and then goes through the tunnel.
- Anywhere uploads and notes require the receive code displayed on the desktop. Download access remains controlled by the random sharing link.
- PC Share starts only when you choose Send or Receive. Send advertises a temporary PC offer on the LAN; phone link tokens are never advertised. Any PC on that LAN can receive a waiting offer, with one receiver at a time. The PC endpoint is unavailable through cloudflared. Stop sharing, return to Phone, change the selected files, or close PocketDrop to revoke the offer. A completed transfer closes automatically. The receiver checks the transferred bytes before saving and never overwrites an existing file.
- Windows verifies the downloaded cloudflared executable's publisher signature. macOS validates its code-signing state. Linux checks that the official HTTPS download is a valid ELF executable before installing it in the user's application-data directory.

## Troubleshooting Anywhere mode

- The full cloudflared output of the latest session is saved in `%LOCALAPPDATA%\PocketDrop\cloudflared.log`.
- Cloudflare's quick-tunnel service sometimes times out. PocketDrop retries startup up to 3 times before showing an error.
- PocketDrop checks that a new tunnel name is live using Cloudflare's DNS-over-HTTPS endpoint directly, not your router. Asking the router too early can make it cache "name does not exist," which breaks the link for phones on the same Wi-Fi until that negative cache expires.

## Layout

| File | Purpose |
|---|---|
| `src/core/` | Bundling, compression, HTTP server, QR encoder, tunnel lifecycle, and platform interface |
| `src/ui/` | Shared app state, layout, painting, hit testing, and vector icons |
| `src/win/` | Win32 windowing, Direct2D graphics, platform services, resources, and entry point |
| `src/mac/` | AppKit windowing, Core Graphics, platform services, bundle metadata, and entry point |
| `src/linux/` | GTK windowing, Cairo/Pango graphics, Linux platform services, icon, and entry point |
| `tests/` | Encoder/zip checks (`core_test` + `verify.py`); HTTP, upload and link-expiry checks (`server_test` + `server_check.py`, run from the repo root); phone page script check (`check_page_js.mjs`) |

## Phone upload protocol

All paths are relative to the phone session URL `/s/<session>/`:

| Request | Purpose |
|---|---|
| `POST up?name=<name>&size=<bytes>` | Start an upload. Returns `{"id","received":0,"chunk"}`, or `507 {"error":"disk"}` when there isn't room. |
| `PUT up/<id>?offset=<n>` | Write one chunk (â‰¤ 32 MB; the page uses 8 MB to stay under Cloudflare's 100 MB request limit). Re-sending a chunk is safe; an offset past what was received returns `409 {"received"}`. The final chunk returns `{"done":true,"name"}` with the saved name. |
| `GET up/<id>` | Bytes received so far, for resuming after a dropped connection. |
| `DELETE up/<id>` | Cancel and delete the partial file. |
| `POST text` | Send a note (UTF-8 body, up to 1 MB). |

Partial files are written as `<name>.<id>.pdpart` in the received-files folder and renamed when complete.

When `api/manifest` reports `receiveAuth: true`, send `POST auth` with the displayed receive code as a plain UTF-8 body before using the upload or note endpoints. `204` means authorized, `401` means an incorrect code, and `429` means the session's attempts are exhausted.

## PC Share

1. On the sending PC choose **PC Share → Send**. Add files or a folder, or use the files already in the list. Wait for **Waiting for receiver**.
2. On the receiving PC choose **PC Share → Receive**. It connects automatically when one sender is waiting. With multiple senders, use **Choose sender**.
3. Files save to the configured received-files folder (by default `Downloads/PocketDrop`). Use **Show in folder** when finished. Duplicate names get a numeric suffix. A folder or several files arrive as a ZIP, preserving the folder structure.
4. The top tabs become **Send / Receive / Phone**. **Phone** returns to normal QR sharing. **Stop sharing** or **Cancel** ends an active PC operation. Interrupted transfers can be retried with Receive; cancellation removes the partial download.

Both PCs need the updated build and the same Wi-Fi or wired LAN. Allow PocketDrop on private networks in Windows Firewall. Guest-network client isolation, VPNs and multicast filtering may prevent discovery. TCP serves both phone and PC transfers on port 47291 (or a fallback); UDP discovery uses `239.255.47.29:47292` with a one-hop limit. No additional hosting port is required. PC Share is LAN-only; Anywhere remains the phone/browser connection mode.
