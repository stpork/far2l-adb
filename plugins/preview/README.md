# far2l Preview Plugin

Fast, native image viewer plugin with hardware-accelerated decoding and inline terminal graphics support for [far2l](https://github.com/elfmz/far2l).

Supports macOS, Linux, and BSD operating systems.

---

## Features

- **Blazing Fast Performance**:
  - **Zero-delay image browsing (0 ms)**: Multi-threaded asynchronous prefetch worker automatically predecodes adjacent images (next/previous) in the background so navigating through photo folders is instant.
  - **Zero-copy rendering**: SIMD-accelerated RGBA/BGRA byte-swapping and direct streaming chunked IPC to minimize memory copies and IPC overhead.
  - **Downscaled decode for huge files**: High-resolution photos (48 MP+) are decoded directly at screen resolution when fitting to screen, avoiding massive memory allocations and CPU lag.
- **Dual Decoder Engine**:
  - **macOS**: Native Apple `ImageIO` framework with GPU/hardware acceleration.
  - **Linux / BSD**: Native `GdkPixbuf` engine with automatic fallback to dedicated modular libraries (`libwebp`, `libheif`, `libtiff`, and `stb_image`).
- **Comprehensive Terminal Graphics Protocol Support**:
  - **far2l GUI mode**: High-speed native shared-memory window graphics protocol.
  - **Kitty Graphics Protocol**: Direct 24-bit PNG/RGBA raster transmission with sub-cell placement (`tty_send_kitty`).
  - **iTerm2 Inline Images Protocol**: Base64-streamed PNG graphics compatible with iTerm2 and compatible emulators (`tty_send_iterm2`).
  - **Sixel Graphics Protocol**: 16/256-color rasterization for xterm, foot, mlterm, and mintty (`tty_send_sixel`).
- **Flexible Viewing Modes**:
  - **Fullscreen viewer**: Immersive viewer with customizable compact or standard borders.
  - **QuickView panel (Ctrl+Q)**: Dynamic image inspection in the inactive panel as you move the cursor across files.
  - **Built-in F3 override**: Seamlessly replaces far2l's text/hex viewer for image file extensions.
- **Rich Image Manipulation**:
  - **Zoom**: Smooth continuous zoom, 1% precision zoom, Auto-fit, 100% original pixel mapping, fit-to-width, and fit-to-height.
  - **Pan**: Arrow keys, 1-pixel micro-step pan, and interactive mouse click-and-drag.
  - **Rotation & Mirror**: 90° clockwise/counter-clockwise, fine 1° rotation, horizontal flip, and vertical flip.
  - **EXIF Auto-Orientation**: Automatically normalizes image orientation based on camera orientation metadata tags.
- **Far2l Panel Selection & Batch Culling**:
  - Mark (`Space`), unmark (`Backspace`), or toggle (`Ins`) files directly while viewing.
  - Selections automatically synchronize back to the active panel upon exit (`Esc` or `F10`), allowing immediate batch copying (`F5`), moving (`F6`), or deleting (`F8`).
- **Video Storyboard Generation**:
  - **Fast native 3×3 contact sheets**: When opening or previewing video files, the plugin automatically extracts 9 keyframes across the duration and renders a composite contact sheet with clean timecode badges.
  - **macOS**: 100% native via Apple `AVFoundation` with Metal/GPU hardware decode acceleration (~50–80 ms per video).
  - **Linux / BSD**: In-process `libavformat`/`libavcodec` or zero-temp-file streaming fallback via `ffmpeg`.
  - **External player launch**: Press `Enter` or `O` to immediately open the full video in your default external media player (`mpv`, `IINA`, `VLC`).
- **Full Localization**:
  - Complete English (`previewEng.lng`, `previewEng.hlf`) and Russian (`previewRus.lng`, `previewRus.hlf`) language and help documentation.

---

## How It Works

### macOS Architecture
On macOS, the plugin leverages Apple's `ImageIO` framework (`CGImageSourceCreateWithURL`, `CGImageSourceCreateThumbnailAtIndex`).
1. **Thumbnail-Scaled Decoding**: When an image exceeds the viewport and auto-fit is active, `kCGImageSourceThumbnailMaxPixelSize` instructs the hardware decoder to downscale during decode, cutting memory usage and decode time by up to 10× on large DSLR/RAW/phone photos.
2. **Native Color & Alpha**: Extracts 32-bit RGBA pixel buffers directly from `CGContext` with premultiplied alpha handling.
3. **Format Support**: Hardware-accelerated decoding for JPEG, PNG, HEIC/HEIF, TIFF, WebP, GIF, and macOS-supported image formats.

### Linux & BSD Architecture
On Linux and BSD, the plugin operates a resilient multi-tier pipeline:
1. **System GdkPixbuf**: When `GdkPixbuf` is present, it loads all desktop-supported image formats.
2. **Modular Fallback Engines**:
   - **libwebp**: Supports fast decoding with scale-on-decode (`config.scaled_width/height`).
   - **libheif**: Decodes modern HEIC, HEIF, and AVIF photos.
   - **libtiff**: Decodes complex uncompressed and compressed multi-page TIFFs.
   - **stb_image**: Statically bundled zero-dependency fallback for JPEG, PNG, BMP, TGA, PSD, HDR, and GIF.

### Asynchronous Prefetching Pipeline
When viewing image $N$ in a directory:
1. The plugin analyzes your navigation direction (e.g., advancing forward).
2. A background worker thread (`PrefetchWorker`) immediately loads and decodes image $N+1$.
3. When you press `Right` or `Space`, image $N+1$ is swapped into view with **0 ms latency**.
4. The worker thread immediately pre-fetches image $N+2$.

---

## Supported Formats

| Format | Extensions | Primary Decoder (macOS) | Primary Decoder (Linux) | Fallback |
|---|---|---|---|---|
| **JPEG** | `.jpg`, `.jpeg` | Apple ImageIO (HW) | GdkPixbuf / stb_image | stb_image |
| **PNG** | `.png` | Apple ImageIO (HW) | GdkPixbuf / stb_image | stb_image |
| **WebP** | `.webp` | Apple ImageIO / libwebp | libwebp / GdkPixbuf | libwebp |
| **HEIC / HEIF** | `.heic`, `.heif` | Apple ImageIO (HW) | libheif | libheif |
| **TIFF** | `.tif`, `.tiff` | Apple ImageIO (HW) | libtiff | libtiff |
| **GIF** | `.gif` | Apple ImageIO | GdkPixbuf / stb_image | stb_image |
| **BMP / ICO** | `.bmp`, `.ico` | Apple ImageIO | GdkPixbuf / stb_image | stb_image |
| **TGA / PSD / HDR** | `.tga`, `.psd`, `.hdr` | stb_image | stb_image | stb_image |
| **Video Storyboards** | `.mp4`, `.mkv`, `.mov`, `.avi`, `.webm`, `.m4v` | Apple AVFoundation (HW) | libavcodec / FFmpeg | FFmpeg pipe |

---

## Build

From your far2l build directory:

```bash
cmake --build . --target preview
```

Or using `ninja`:

```bash
ninja preview
```

### Build Requirements & Dependencies
- **macOS**:
  - Clang with C++17 support.
  - Apple frameworks: `ApplicationServices`, `CoreFoundation`, `CoreGraphics`, `ImageIO` (included with Xcode / Command Line Tools).
  - Optional Homebrew packages: `webp`, `libheif`, `libtiff` (recommended for fallback coverage).
- **Linux**:
  - GCC or Clang with C++17 support.
  - Optional development packages:
    - Debian/Ubuntu: `sudo apt install libwebp-dev libheif-dev libtiff-dev libgdk-pixbuf2.0-dev`
    - Fedora/RHEL: `sudo dnf install libwebp-devel libheif-devel libtiff-devel gdk-pixbuf2-devel`
    - Arch Linux: `sudo pacman -S libwebp libheif libtiff gdk-pixbuf2`

---

## Installation

### macOS
Copy the built plugin directory to the far2l application bundle:

```bash
cp -R install/Plugins/preview /Applications/far2l.app/Contents/MacOS/Plugins/
```

### Linux / BSD
- **System-wide**:
  ```bash
  sudo cp -R install/Plugins/preview /usr/lib/far2l/Plugins/
  ```
- **Per-user**:
  ```bash
  mkdir -p ~/.local/lib/far2l/Plugins
  cp -R install/Plugins/preview ~/.local/lib/far2l/Plugins/
  ```

Restart far2l after copying. The plugin will be detected automatically.

---

## Usage

1. **Viewing Files**:
   - **F3**: Press `F3` on any image file (when *Override built-in F3 viewer* is enabled in settings).
   - **Enter**: Press `Enter` on an image (when *Open with Enter* is enabled).
   - **Ctrl+PgDn**: Press `Ctrl+PgDn` on an image file.
   - **QuickView**: Press `Ctrl+Q` on one panel to enable live preview as you browse files on the opposite panel.
   - **Command Line**: Type `preview:<path_to_image>` into far2l's prompt and press Enter.
   - **F11 Menu**: Press `F11` -> select **Preview**.

2. **Culling / Selecting Photos**:
   - As you view photos with `Right` / `Space`, press `Space` on photos you want to keep or tag.
   - Press `Backspace` to unmark and step back.
   - Press `Esc` to return to the panel. All marked photos are selected in far2l, ready for `F5` (Copy), `F6` (Move), or `F8` (Delete).

3. **External Viewer**:
   - Press `Enter` or `O` inside the viewer to open the current image in the system's default viewer (`open` on macOS, `xdg-open` on Linux).

---

## Key Bindings

| Key | Action |
|---|---|
| **Navigation** | |
| `Right` / `Down` | Next image in current directory |
| `Left` / `Up` | Previous image in current directory |
| `Home` | First image in directory (when not rotated) |
| `End` | Last image in directory (when not rotated) |
| `PgDn` / `PgUp` | Next / Previous image |
| **Zoom & Fit** | |
| `PgUp` / `+` / `Num+` | Zoom in |
| `PgDn` / `-` / `Num-` | Zoom out |
| `Shift+PgUp` | Fine zoom in (+1%) |
| `Shift+PgDn` | Fine zoom out (-1%) |
| `*` / `A` | Auto-fit image to viewport |
| `/` / `Z` | 100% original size (1:1 pixel mapping) |
| `S` | Cycle fit mode: Auto → Fit Width → Fit Height → 100% |
| `=` / `Num5` | Reset zoom and pan offsets to default |
| `Mouse Wheel` | Smooth zoom in / out |
| **Panning** | |
| `Alt+Arrows` | Pan viewport across zoomed image |
| `Alt+Shift+Arrows` | Fine pan (1 pixel per keypress) |
| `Left Mouse Drag` | Interactive click-and-drag panning |
| **Rotation & Flipping** | |
| `Home` | Rotate 90° counter-clockwise |
| `End` | Rotate 90° clockwise |
| `Shift+Home` | Fine rotate 1° counter-clockwise |
| `Shift+End` | Fine rotate 1° clockwise |
| `Tab` / `Shift+Tab` | Rotate 90° clockwise / counter-clockwise |
| `F7` / `H` | Mirror horizontally (flip X) |
| `F8` / `V` | Mirror vertically (flip Y) |
| `Right Mouse Click` | Rotate 90° clockwise |
| **Selection & Culling** | |
| `Ins` | Toggle selection for current file |
| `Space` | Select current file and advance to next image |
| `Backspace` | Deselect current file and step back to previous image |
| **General** | |
| `Enter` / `O` | Open in external default system application |
| `F5` / `F` | Toggle fullscreen mode |
| `F9` | Open Configuration / Settings dialog |
| `F1` | Show context-sensitive help (`previewEng.hlf` / `previewRus.hlf`) |
| `Esc` / `F10` | Exit viewer and apply selection to far2l panel |

---

## Configuration Options (F9)

Press **F9** while viewing any image, or configure via **F11 → Options → Preview**:

- **Enable plugin**: Master switch to enable or disable plugin hooks.
- **Apply EXIF orientation**: Read camera metadata tags and automatically orient photos upright.
- **Open with Enter**: Open images in Preview when pressing `Enter` on the file list.
- **Open with Ctrl+PgDn**: Open images in Preview when pressing `Ctrl+PgDn`.
- **Show in QuickView**: Render live image previews inside far2l's QuickView panel (`Ctrl+Q`).
- **Override built-in F3 viewer**: Intercept `F3` on image files instead of opening the internal viewer.
- **Auto-fit after rotate**: Recalculate zoom fitting whenever an image is rotated by 90° or 270°.
- **Fast transforms**: Use high-speed nearest-neighbor / integer scaling for real-time responsiveness.
- **Compact frame**: Hide extra frame margins to maximize the visible image area.
- **Use OS image codec**: Use hardware-accelerated OS decoders (Apple ImageIO on macOS, GdkPixbuf on Linux).
- **Default fit mode**: Select default scaling on file open:
  - *Auto*: Fit whole image within the window while maintaining aspect ratio.
  - *Width*: Fit image width to window width.
  - *Height*: Fit image height to window height.
  - *Original*: 1:1 pixel mapping (100%).
- **Image masks**: Space-separated list of wildcard file patterns (e.g. `*.jpg *.jpeg *.png *.gif *.webp *.heic *.tiff *.bmp`).

---

## Terminal Protocol Compatibility

| Terminal Emulator | Protocol | Support | Notes |
|---|---|---|---|
| **far2l GUI** | Native far2l IPC | Full | Maximum performance, zero terminal escape overhead |
| **Kitty** | Kitty Graphics Protocol | Full | Direct RGBA / PNG transmission, smooth rendering |
| **Ghostty** | Kitty Graphics Protocol | Full | High-speed GPU-accelerated rendering |
| **WezTerm** | Kitty / iTerm2 | Full | Auto-detected protocol negotiation |
| **iTerm2** | iTerm2 Inline Images | Full | Base64 PNG inline streaming |
| **Foot** (Wayland) | Sixel | Full | 256-color palette dithering |
| **XTerm** | Sixel | Full | When compiled with sixel support (`-ti vt340`) |
| **Standard TTY / SSH** | None | Dialog fallback | Displays error message: *Terminal lacks graphics support* |

---

## Localization

The plugin is fully localized into English and Russian:
- **Language strings**: `previewEng.lng` and `previewRus.lng`.
- **Online Help (F1)**: `previewEng.hlf` and `previewRus.hlf`.

The plugin automatically follows far2l's interface language setting.

---

## License

Part of [far2l](https://github.com/elfmz/far2l). Licensed under the GNU General Public License v2 (GPLv2).
