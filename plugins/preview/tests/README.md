# macOS image clipboard regression

From the repository root, compile and run with the macOS SDK:

```sh
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined \
  plugins/preview/tests/image-clipboard-mac.mm plugins/preview/src/Image.cpp \
  -framework AppKit -framework Accelerate -o /tmp/preview-image-clipboard-test
/tmp/preview-image-clipboard-test
```

This checks PNG and TIFF pixel round trips, rotation/mirroring, replacement,
clipboard ownership after releasing the source image, and preserving clipboard
contents on invalid input. It uses a private pasteboard, leaving the system
clipboard untouched.

For an end-to-end check, open an image in Preview, rotate or mirror it, press
Cmd+C (or Ctrl+C/Ctrl+Insert), then use Preview.app's File → New from Clipboard.
The whole rendered image, including zoom and transforms, should appear. Repeat
after navigating to another image. Terminal emulators must forward the shortcut
to far2l; a terminal's own Copy action cannot invoke the plugin.

## Optional codecs and macOS fallback

Install libheif development headers and FFmpeg on the test machine, then run:

```sh
bash plugins/preview/tests/run-optional-codecs.sh image.png image.heic video.mp4
```

The fixtures must be readable files; HEIC/AVIF needs an installed libheif decoder
and the video must contain enough frames for a 3×3 storyboard. On macOS use a
video also supported by AVFoundation. The runner uses ASan/UBSan and temporary
binaries. It checks missing libheif, a missing API symbol, retry after installation,
PNG decoding with no libheif, cancellation, absent FFmpeg, and real CLI decoding.
On macOS it additionally tests both backend priorities, native HEIC/video fallback
with the external components hidden, and FFmpeg discovery with a Finder-like PATH.
Library/process wrappers simulate missing dependencies without changing the host.
