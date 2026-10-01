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
