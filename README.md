# PocketDrop

Drop files on your PC, scan the QR code, download on your phone. No phone app, no account.

A single ~0.5 MB native Windows executable (C++20, Win32 + Direct2D, static CRT, no runtime dependencies).

## Features

- **Drop anything**: files, folders, dragged text, or **Ctrl+V** for copied files, text, or screenshots (saved as PNG).
- **Smart packaging**:
  - A single file downloads as itself.
  - Several files or any folder also get a **Download all** zip that keeps the folder structure.
  - Each file is compressed only if that pays off: already-compressed formats (JPEG, MP4, ZIP, DOCX, ...) are stored as-is, text-like formats are deflated, and unknown types are sample-tested first.
  - CRCs and compression are computed in parallel in the background, so the zip streams immediately with an exact size, resumable range requests, and zip64 for very large bundles.
- **Two ways to connect**:
  - **Same Wi-Fi**: direct LAN transfer, fastest.
  - **Anywhere**: a Cloudflare quick tunnel (`https://*.trycloudflare.com`), so a phone on mobile data can reach the PC. It needs no port forwarding, no account, and no VPN app on the phone. `cloudflared` is fetched once on request, and its code signature is checked.
- **Phone page**:
  - The file list shows thumbnails for photos, videos and PDFs, rendered by Windows' thumbnail handlers.
  - Buttons: per-file download, **Download all**, copy (and open, for links) on shared text, and **Save to Photos** over HTTPS.
  - Updates live as you add files and adapts to light or dark mode.
- **Desktop**:
  - Live transfer progress and speed, plus a "phone connected" indicator.
  - "Stop sharing after a download", **New link** (revokes the old one), network address picker, always on top.
  - Optional **Send to → PocketDrop** shortcut. The app is single-instance, so Send to adds to the open window.

## Build

Requires Visual Studio 2022 Build Tools with the C++ workload.

```bat
build.bat
```

Output: `build\PocketDrop.exe`. You can also pass paths on the command line: `PocketDrop.exe file1 folder2`.

On first launch Windows asks whether to allow network access. Choose **Allow** for private networks, or phones on your Wi-Fi can't connect.

## Security model

- Every link has a random 128-bit token. Wrong tokens get a plain 404.
- Links die when PocketDrop closes, when you choose **New link**, or after a download if that option is on.
- Responses carry `Referrer-Policy: no-referrer` and a strict CSP. HTML and SVG previews are sandboxed.
- Same Wi-Fi mode is plain HTTP on your LAN. Anywhere mode is HTTPS from the phone to Cloudflare, and then goes through the tunnel.

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
| `tests/` | Encoder/zip checks (`core_test` + `verify.py`), HTTP checks (`server_test` + `server_check.py`) |

## Phase 2 (two-way) notes

The server already routes by token and tracks transfers, so uploads fit in as `POST /<token>/up`.

Cloudflare limits request bodies to 100 MB on quick tunnels, so uploads from the phone should be sent in chunks, for example 8 MB `PUT`s with an offset, then assembled on the PC.
