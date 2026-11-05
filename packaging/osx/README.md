# macOS application and DMG builds

From the repository root, with the Homebrew dependencies installed:

```sh
./build.sh release       # standalone _release/package/far2l.app
./build.sh release dmg   # the same app preparation, plus _release/*.dmg
./build.sh debug         # standalone _debug/package/far2l.app, without stripping
```

The `install/far2l.app` inside each build directory is the working build output.
The `package/far2l.app` is the distributable copy with its bundled dependencies
and signature. Packaging does not strip or rewrite the working build output.

Release keeps optimization and compiles with `-g`. Before signing, packaging
generates dSYMs for the executable, GUI backend, plugins and broker executables.
It checks their UUIDs and DWARF compile units, then uses only `strip -S` on the
packaged Release copies. Local/global function names, UUIDs and unwind data stay
in the binaries so the built-in `crash.log` can resolve names with `dladdr()`.
Debug packages retain the complete symbol tables and debug maps. Third-party
libraries are copied without additional stripping.

dSYMs and JSON manifests are archived under `.symbols/`, outside the application
and DMG. The directory is ignored by Git and preserved by `build.sh clean/clear`.
Keep a backup of it for every distributed version. Each manifest records the
configuration, Git revision/dirty state, relative image paths, architecture UUIDs
and matching dSYM paths. UUID directories retain earlier builds instead of
replacing them when a new release is compiled.

For a different archive location:

```sh
FAR2L_SYMBOLS_DIR=/path/to/symbol-archive ./build.sh release dmg
```

To build and prepare an app directly with CMake:

```sh
cmake -S . -B _release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build _release
FAR2L_CODESIGN_CERT=- cmake --install _release --component app --prefix "$PWD/_release/package"
```

`FAR2L_CODESIGN_CERT` selects the signing identity; `build.sh` defaults to ad-hoc
signing (`-`). Signature errors stop packaging. Do not use `cmake --install --strip`
or `CPACK_STRIP_FILES`; they can remove local names needed by the crash logger.

For offline symbolication, locate the image in the manifest and check the UUID:

```sh
dwarfdump --uuid /path/to/binary
dwarfdump --uuid /path/to/matching.dSYM
atos -arch arm64 -o /path/to/matching.dSYM/Contents/Resources/DWARF/binary -l LOAD_ADDRESS FRAME_ADDRESS
```

Use the load address from the crash report; module-relative offsets must first
be converted to the image's linked address space. A dSYM is useful only for the
matching build UUID. The built-in logger does not load dSYMs itself.

On the native macOS printing path, wxWidgets HTML/rich-text libraries are not
linked or bundled. The GCC fallback and non-macOS printing paths retain them.
Colorer HRC files remain unpacked and editable.
Python imports do not write bytecode caches inside the signed macOS bundle.

Run the packaging regression checks on macOS:

```sh
python3 -B testing/macos/test_package_symbols.py
```
