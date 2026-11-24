# 32-bit Windows Virtual TWAIN Scanner

> **Important Notice — Unmaintained, No Liability, AI-Assisted Code**
> - **Not Maintained:** This repository is published for personal/educational sharing only and is **not actively maintained**. Issues, pull requests, and support requests may not be answered.
> - **No Warranty / No Liability:** This software and its prebuilt binaries are provided **"AS IS"**, without warranty of any kind. The author(s) and contributors accept **no liability** for any direct, indirect, incidental, or consequential damages, system issues, data loss, or third-party terms-of-service / compliance issues arising from the use or misuse of this software. Use entirely at your own risk.
> - **Mostly AI-Written:** The custom extensions, UI rewrite, PDFium integration, modal `force_ui` host, and GitHub Actions build workflow in this fork were **mostly written with AI assistance** on top of the upstream TWAIN Working Group sample driver. Review the source code yourself before trusting or deploying it.

---

## What It Does

This project builds a **32-bit Windows TWAIN Data Source** (`TWAINDS_Sample32.ds`) that registers as **`Windows Virtual Scanner`** in 32-bit TWAIN applications (such as NAPS2, Twacker, or local web-to-TWAIN scanner bridges). Instead of reading from physical scanner hardware, it lets you pick existing **PDF documents and image files** from your computer and stream them into the calling application as scanned pages.

### Features
- **Qt Multi-File Picker Dialog:** Queue one or more files (`.pdf`, `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, `.tiff`), reorder or remove items, and see total page counts before scanning.
- **Direct 32-bit PDF & Multi-Page TIFF Rendering:** Renders multi-page PDFs directly at the chosen DPI using 32-bit PDFium (`pdfium.dll`) and reads multi-page TIFFs page by page via FreeImage (`FreeImage.dll`).
- **Scan Controls:** Choose **Resolution (DPI)**, **Color Mode** (Color, Grayscale, Black & White), and **Multi-Page Mode** (All Pages via ADF, First Page Only, or Custom Page Range such as `1-3` or `1,3,5`).
- **Forced UI (`"force_ui": true`) for Headless TWAIN Callers:** When a TWAIN application or web scanner bridge calls `MSG_ENABLEDS` with `ShowUI = FALSE`, enabling `"force_ui": true` in `source.json` opens the Qt file-picker dialog anyway — running modally on a dedicated persistent STA Qt UI thread so callers do not need a Win32 message loop.
- **Always-On-Top Windowing:** Centers the picker dialog on the active monitor and raises/activates it above browser and desktop windows (`Qt::WindowStaysOnTopHint`, `raise()`, `activateWindow()`, and Win32 topmost/foreground activation).
- **Automated 32-bit CI Build:** [`.github/workflows/build-x86.yml`](.github/workflows/build-x86.yml) compiles `Release | Win32` with MSVC x86 and 32-bit Qt 5.15.2, bundles all matching 32-bit runtime DLLs and licenses, verifies every `.ds`/`.dll` PE header is 32-bit x86 (`0x014C`), and uploads `VirtualScanner-win32-portable`.

---

## Installation (Windows 32-bit TWAIN)

1. Download the `VirtualScanner-win32-portable` artifact from **Actions → Build 32-bit TWAINDS_Sample32.ds**.
2. Extract all files into:
   ```text
   C:\Windows\twain_32\sample2\
   ```
   *(Administrator privileges are required to copy into `C:\Windows\twain_32\`.)*
3. Ensure a 32-bit TWAIN Data Source Manager (`twain_32.dll` or `twaindsm.dll`) is present on Windows.
4. Select **Windows Virtual Scanner** inside your 32-bit TWAIN application.

---

## Configuration (`source.json`)

Place `source.json` in the same folder as `TWAINDS_Sample32.ds` (`C:\Windows\twain_32\sample2\source.json`):

```json
{
  "folder": "C:/Scan",
  "force_ui": true
}
```

- **`force_ui`** (`true` / `false`): When `true`, opens the modal Qt file-picker dialog even if the calling TWAIN host sends `ShowUI = FALSE`.
- **`folder`** (optional string): Fallback folder of image files used only when `ShowUI = FALSE` and `force_ui` is `false`. If omitted or empty, the driver falls back to `TWAIN_logo.png`.

### Diagnostics
Lifecycle logs (`enableDS`, modal picker status, `DoXferReadyEvent`, `disableDS`) are written to:
```text
%LOCALAPPDATA%\VirtualScanner\scanner.log
```
(automatically truncated when exceeding 128 KB).

---

## Credits & Attribution

Full credit for the underlying TWAIN driver architecture and bundled libraries belongs to their original authors and maintainers:

- **TWAIN Working Group** — Original [`twain/twain-samples`](https://github.com/twain/twain-samples) C++ TWAIN Data Source (`Twain_DS_sample01`), licensed under the **BSD 3-Clause License**.
- **The Qt Company / Qt Project** — [Qt 5.15.2](https://www.qt.io/) dynamically linked runtime (`Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`, `qwindows.dll`), used under the **GNU LGPL v3.0**. Matching Qt 5.15.2 source code is available from [download.qt.io](https://download.qt.io/official_releases/qt/5.15/5.15.2/single/) and [code.qt.io](https://code.qt.io/cgit/qt/qt5.git/tag/?h=v5.15.2).
- **FreeImage Project** — [FreeImage](https://freeimage.sourceforge.io) image loading and conversion library (`FreeImage.dll`), used under the **FreeImage Public License (FIPL) v1.0**.
- **PDFium / Google & Benoit Blanchon** — [PDFium](https://pdfium.googlesource.com/pdfium/) and prebuilt 32-bit Windows binaries from [`bblanchon/pdfium-binaries`](https://github.com/bblanchon/pdfium-binaries), licensed under **Apache-2.0 / BSD 3-Clause**.
- **Niels Lohmann** — [JSON for Modern C++ (`nlohmann/json`)](https://github.com/nlohmann/json), licensed under the **MIT License**.

*This project is an independent, unofficial fork and is not endorsed by or affiliated with the TWAIN Working Group or any of the organizations listed above.*

---

## License

- Source code in this repository is licensed under the **BSD 3-Clause License** — see [`LICENSE`](LICENSE).
- Full third-party license notices are included in [`THIRD_PARTY_LICENSES.txt`](THIRD_PARTY_LICENSES.txt) and bundled inside the portable ZIP artifact.
