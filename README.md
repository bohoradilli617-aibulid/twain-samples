# 32-bit Windows Virtual TWAIN Scanner

A 32-bit Windows TWAIN Data Source (`TWAINDS_Sample32.ds`) forked from the official [TWAIN Working Group `twain-samples`](https://github.com/twain/twain-samples) (BSD 3-Clause), extended with:

- **Qt Multi-File Picker UI** — queue multiple PDF, PNG, JPEG, BMP, and single/multi-page TIFF files, reorder them, and choose Resolution (DPI), Color Mode (Color / Grayscale / B&W), and Multi-Page Mode (All Pages, First Page Only, or Custom Page Range).
- **Direct 32-bit PDF Rendering** — renders PDF pages at the selected DPI using 32-bit PDFium (`pdfium.dll`).
- **`"force_ui": true` Support (`source.json`)** — opens the file-picker dialog even when a TWAIN host or web scanner bridge calls `MSG_ENABLEDS` with `ShowUI = FALSE`.
- **Modal Persistent STA Qt Host & Always-On-Top Windowing** — runs forced UI modally on a dedicated STA Qt thread inside `enableDS` (so callers do not need a Win32 message pump), centers on the active monitor, and brings the dialog above browser windows (`Qt::WindowStaysOnTopHint`, `raise()`, `activateWindow()`, and Win32 topmost/foreground activation).
- **Automated 32-bit CI Build** — GitHub Actions workflow (`.github/workflows/build-x86.yml`) compiles `Release | Win32` with MSVC x86 + Qt 5.15.2, bundles all 32-bit DLLs, verifies every `.ds`/`.dll` PE header is 32-bit (`0x014C`), and uploads `VirtualScanner-win32-portable`.

## Installation (Windows 32-bit TWAIN)

1. Download the `VirtualScanner-win32-portable` artifact from **Actions → Build 32-bit TWAINDS_Sample32.ds**.
2. Extract all files into:
   ```text
   C:\Windows\twain_32\sample2\
   ```
   *(Administrator privileges are required to copy into `C:\Windows\twain_32\`.)*
3. Ensure a 32-bit TWAIN Data Source Manager (`twain_32.dll` / `twaindsm.dll`) is installed on the machine.
4. Select **Windows Virtual Scanner** in your 32-bit TWAIN application (e.g., NAPS2 or a local TWAIN bridge).

## Configuration (`source.json`)

Place `source.json` next to `TWAINDS_Sample32.ds` (`C:\Windows\twain_32\sample2\source.json`):

```json
{
  "folder": "C:/Scan",
  "force_ui": true
}
```

- **`force_ui`** (`true` / `false`): When `true`, opens the modal Qt file-picker dialog even if the calling TWAIN application sends `ShowUI = FALSE`.
- **`folder`** (optional string): Fallback folder of image files used when `ShowUI = FALSE` and `force_ui` is `false`. If omitted or empty, falls back to `TWAIN_logo.png`.

## Diagnostics

Lifecycle logs for `enableDS` / modal picker / `disableDS` are written to:
```text
%LOCALAPPDATA%\VirtualScanner\scanner.log
```
(automatically truncated when exceeding 128 KB).

## License & Third-Party Notices

- Source code is licensed under the **BSD 3-Clause License** — see [`LICENSE`](LICENSE).
- Bundled third-party libraries (Qt 5.15.2 under LGPLv3, FreeImage under FIPL 1.0, PDFium under Apache-2.0/BSD-3-Clause, and `nlohmann/json` under MIT) are credited in [`THIRD_PARTY_LICENSES.txt`](THIRD_PARTY_LICENSES.txt).
