# 32-bit Windows Virtual TWAIN Scanner

> **Notice:** This project is **not actively maintained**, is provided **"AS IS" with no warranty and no liability** for any damages or issues resulting from its use, and its custom modifications were **mostly written with AI assistance**. Use at your own risk.

## What It Does

A **32-bit Windows TWAIN Data Source** (`TWAINDS_Sample32.ds`) that registers as **Windows Virtual Scanner** in 32-bit TWAIN applications and web-to-TWAIN scanner bridges, allowing you to scan existing PDF and image files instead of using physical hardware.

- **Multi-File Picker UI:** Select and reorder multiple `.pdf`, `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, and `.tiff` files.
- **Network Scanner (eSCL / AirScan):** Pull live scans directly from eSCL/AirScan network MFPs (Flatbed, ADF Simplex, ADF Duplex; A4/A5/Letter/Full Glass) over HTTP/HTTPS into 32-bit TWAIN apps, with optional one-click **Auto-send**.
- **PDF & Multi-Page TIFF Support:** Renders PDF pages at the selected DPI via 32-bit PDFium and reads multi-page TIFFs via FreeImage.
- **Scan Controls:** Resolution (DPI), Color Mode (Color / Grayscale / Black & White), and Multi-Page Mode (All Pages, First Page Only, or Custom Page Range).
- **Forced UI (`"force_ui": true`):** Opens the modal Qt dialog on top of desktop/browser windows even when the calling TWAIN host sends `ShowUI = FALSE`.

## Installation

1. Download **`VirtualScanner-win32-portable.zip`** from **[Releases](https://github.com/bohoradilli617-aibulid/twain-samples/releases/latest)** and extract it.
2. Close any running scanner application or TWAIN bridge.
3. Run **`install.cmd`** (as Administrator) to install into `C:\Windows\twain_32\sample2\`, or copy the extracted files there manually.
4. Select **Windows Virtual Scanner** in your 32-bit TWAIN application (requires a 32-bit TWAIN Data Source Manager such as `twain_32.dll` or `twaindsm.dll`).

To uninstall, run `C:\Windows\twain_32\sample2\uninstall.cmd` (as Administrator) or delete `C:\Windows\twain_32\sample2\`.

## Configuration (`source.json`)

Edit `C:\Windows\twain_32\sample2\source.json`:

```json
{
  "folder": "C:/Scan",
  "force_ui": true
}
```

- `force_ui`: When `true`, opens the dialog even if the host requests `ShowUI = FALSE`.
- `folder`: Optional fallback image folder used when `ShowUI = FALSE` and `force_ui` is `false`.
- `escl_url` / `escl_scanners` *(optional)*: Pre-populate network eSCL scanner URLs (or just type a scanner IP directly in the UI — recently used scanners and preferences are saved automatically in `%LOCALAPPDATA%\VirtualScanner\escl_settings.json`).

Logs are written to `%LOCALAPPDATA%\VirtualScanner\scanner.log`.

## Credits

- **TWAIN Working Group** — Upstream [`twain/twain-samples`](https://github.com/twain/twain-samples) (BSD 3-Clause).
- **The Qt Company / Qt Project** — [Qt 5.15.2](https://www.qt.io/) dynamically linked under **LGPL v3.0** ([source archive](https://download.qt.io/official_releases/qt/5.15/5.15.2/single/)).
- **FreeImage Project** — [FreeImage](https://freeimage.sourceforge.io) under **FIPL v1.0**.
- **PDFium / Google & Benoit Blanchon** — [PDFium](https://pdfium.googlesource.com/pdfium/) / [`bblanchon/pdfium-binaries`](https://github.com/bblanchon/pdfium-binaries) (Apache-2.0 / BSD 3-Clause).
- **Niels Lohmann** — [`nlohmann/json`](https://github.com/nlohmann/json) (MIT).

*Unofficial fork; not affiliated with or endorsed by the TWAIN Working Group or any third party listed above.*

## License

- Source code: **BSD 3-Clause License** ([`LICENSE`](LICENSE)).
- Bundled third-party notices: [`THIRD_PARTY_LICENSES.txt`](THIRD_PARTY_LICENSES.txt).
