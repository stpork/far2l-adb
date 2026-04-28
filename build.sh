#!/usr/bin/env bash
set -euo pipefail

export LC_ALL=C
export LANG=C

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARGS=$(echo "$*" | tr 'A-Z' 'a-z')

if [[ "$ARGS" == *"cmake"* ]]; then PREFER_MAKE=1; else PREFER_MAKE=0; fi
if [[ "$ARGS" == *"release"* ]]; then TYPE=Release; else TYPE=Debug; fi
if [[ "$ARGS" == *"dmg"* ]]; then DO_DMG=1; else DO_DMG=0; fi
if [[ "$ARGS" == *"clean"* ]]; then DO_CLEAN=1; else DO_CLEAN=0; fi

# Install missing tools via Homebrew if needed
need_install=0
if ! command -v cmake >/dev/null 2>&1; then need_install=1; fi
if ! command -v pkg-config >/dev/null 2>&1; then need_install=1; fi
if [[ $PREFER_MAKE -eq 0 ]] && ! command -v ninja >/dev/null 2>&1; then need_install=1; fi

if [[ $need_install -eq 1 ]]; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "Homebrew required. Install from https://brew.sh/ and re-run." >&2
        exit 1
    fi
    brew bundle --file="${SCRIPT_DIR}/Brewfile"
    brew link libarchive --force >/dev/null 2>&1 || true
fi

# Resolve generator
if [[ $PREFER_MAKE -eq 1 ]] || ! command -v ninja >/dev/null 2>&1; then
    GEN="Unix Makefiles"
    NCPU=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    BUILD="make -j${NCPU}"
else
    GEN="Ninja"
    BUILD="ninja"
fi

DIR="_$(echo "$TYPE" | tr 'A-Z' 'a-z')"

# Recreate build dir if generator changed or build files are missing
if [[ -f "${DIR}/CMakeCache.txt" ]]; then
    CACHED_GEN=$(grep "^CMAKE_GENERATOR:INTERNAL=" "${DIR}/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || true)
    if [[ -n "$CACHED_GEN" && "$CACHED_GEN" != "$GEN" ]]; then
        echo "Generator changed (${CACHED_GEN} → ${GEN}), recreating ${DIR}..."
        rm -rf "$DIR"
    elif [[ "$GEN" == "Ninja" && ! -f "${DIR}/build.ninja" ]] || \
         [[ "$GEN" == "Unix Makefiles" && ! -f "${DIR}/Makefile" ]]; then
        echo "Build files missing, reconfiguring ${DIR}..."
        rm -f "${DIR}/CMakeCache.txt"
    fi
fi

if [[ $DO_CLEAN -eq 1 ]]; then rm -rf "$DIR"; fi

mkdir -p "$DIR" && cd "$DIR"

if [[ ! -f "CMakeCache.txt" ]]; then
    cmake -G "$GEN" -DCMAKE_BUILD_TYPE="$TYPE" \
        -DPYTHON=yes \
        -DCMAKE_PREFIX_PATH="/opt/homebrew" \
        -DCMAKE_INSTALL_RPATH="/opt/homebrew/lib" \
        -DCMAKE_BUILD_RPATH="/opt/homebrew/lib" \
        -DCMAKE_INSTALL_PREFIX="install" \
        ..
fi

$BUILD

if [[ $DO_DMG -eq 1 ]]; then
    $BUILD install

    # Regenerate + patch the build-tree FixupBundle.cmake from the .cmake.in template.
    # ICU libs cross-reference via @loader_path which cmake's BundleUtilities passes
    # literally to otool, causing a fatal error. Fix: supply DIRS and add ICU to IGNORE_ITEM
    # so BundleUtilities doesn't recurse into ICU's internal deps. The pre-seeded Frameworks
    # copies (with absolute paths) satisfy runtime resolution.
    FIXUP_IN="${SCRIPT_DIR}/packaging/osx/FixupBundle.cmake.in"
    FIXUP="packaging/osx/FixupBundle.cmake"
    if [[ -f "$FIXUP_IN" ]]; then
        ICU_LIB=$(brew --prefix icu4c 2>/dev/null)/lib
        python3 - "$FIXUP_IN" "$FIXUP" "/opt/homebrew/lib" "$ICU_LIB" <<'PYEOF'
import sys
tmpl, out, hblib, iculib = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
icu_ignore = "libicudata.78.dylib;libicuuc.78.dylib;libicui18n.78.dylib;libicuio.78.dylib;libicutu.78.dylib;libicutest.78.dylib"
with open(tmpl) as f:
    txt = f.read()
txt = txt.replace('@APP_NAME@', 'far2l')
txt = txt.replace(
    'fixup_bundle("${APP_INSTALL_DIR}" "${PLUGINS}" "" IGNORE_ITEM "python;python3;python3.8;Python;.Python")',
    f'set(DIRS "{hblib}" "{iculib}")\nfixup_bundle("${{APP_INSTALL_DIR}}" "${{PLUGINS}}" "${{DIRS}}" IGNORE_ITEM "python;python3;python3.8;Python;.Python;{icu_ignore}")'
)
# --force is required: fixup_bundle invalidates existing signatures via install_name_tool,
# so codesign must replace them (without --force it silently skips already-signed files)
txt = txt.replace(
    'COMMAND /usr/bin/codesign -s "${CODESIGN_CERT}" --deep',
    'COMMAND /usr/bin/codesign --force -s "${CODESIGN_CERT}" --deep'
)
with open(out, 'w') as f:
    f.write(txt)
PYEOF
    fi

    # Pre-seed Frameworks with ICU dylibs, rewriting @loader_path to absolute paths so
    # cmake BundleUtilities can resolve them (it passes @loader_path literally to otool)
    ICU_LIB=$(brew --prefix icu4c 2>/dev/null)/lib
    FRAMEWORKS="${PWD}/install/far2l.app/Contents/Frameworks"
    mkdir -p "$FRAMEWORKS"
    cp -n "${ICU_LIB}"/libicu*.78.dylib "$FRAMEWORKS/" 2>/dev/null || true
    for lib in "${FRAMEWORKS}"/libicu*.78.dylib; do
        for dep in "${FRAMEWORKS}"/libicu*.78.dylib; do
            depname=$(basename "$dep")
            install_name_tool -change "@loader_path/${depname}" "${dep}" "$lib" 2>/dev/null || true
        done
    done

    find "install/far2l.app" -name "CMakeFiles" -type d -exec rm -rf {} + 2>/dev/null || true

    # Sign AFTER fixup_bundle (inside cpack) so install_name_tool doesn't void the signature.
    # FixupBundle.cmake reads FAR2L_CODESIGN_CERT and signs at the end of the install step.
    FAR2L_CODESIGN_CERT=- cpack -G DragNDrop 2> >(grep -v "warning: changes being made to the file will invalidate the code signature" >&2)
fi
