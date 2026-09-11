#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C LANG=C

SD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
usage() {
    cat <<EOF >&2
Usage: $0 [debug|release] [full] [dmg] [clean|clear]
       $0 clear|clean

Commands:
  debug|release  Build the specified configuration (default: debug)
  full           Enable AWS, SMB, and NFS netrocks plugins
  dmg            Package macOS DragNDrop DMG bundle
  clean|clear    When used alone, remove all build artifacts, junk, and caches.
                 When used with debug/release, clean the build directory first.
EOF
    exit 1
}

clean_all() {
    echo "Cleaning all build artifacts, temporary files, and caches..."
    rm -rf "$SD"/_debug "$SD"/_release "$SD"/_build "$SD"/build "$SD"/cmake-build-*
    rm -rf "$SD"/_CPack_Packages "$SD"/install "$SD"/install_manifest.txt "$SD"/*.dmg
    rm -rf "$SD"/.cache
    rm -rf "$SD"/python/staging "$SD"/python/python "$SD"/python/incpy "$SD"/tools
    rm -f "$SD"/CPackConfig.cmake "$SD"/CPackSourceConfig.cmake
    rm -f "$SD"/packaging/osx/FixupBundle.cmake "$SD"/packaging/osx/Setup.scpt
    rm -f "$SD"/far2l/far2l_askpass "$SD"/far2l/far2l_sudoapp "$SD"/far2l_askpass "$SD"/far2l_sudoapp "$SD"/far2l/far2ledit

    if git -C "$SD" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$SD" clean -fdx
    else
        find "$SD" \
            \( -path '*/.git' -o -path '*/.vscode' \) -prune -o \
            \( \
                -name 'CMakeCache.txt' -o \
                -name 'CMakeFiles' -o \
                -name 'CMakeScripts' -o \
                -name 'CMakeLists.txt.user' -o \
                -name 'cmake_install.cmake' -o \
                -name 'CTestTestfile.cmake' -o \
                -name 'compile_commands.json' -o \
                -name 'CPackConfig.cmake' -o \
                -name 'CPackSourceConfig.cmake' -o \
                -name 'Testing' -o \
                -name '.ninja_*' -o \
                -name '*.ninja*' -o \
                -name '*.a' -o \
                -name '*.o' -o \
                -name '*.dylib' -o \
                -name '*.so' -o \
                -name '.DS_Store' -o \
                -name '._*' -o \
                -name '*~' -o \
                -name '*.swp' -o \
                -name '*.swo' -o \
                -name '*.orig' -o \
                -name '*.bak' -o \
                -name '__pycache__' -o \
                -name '*.pyc' -o \
                -name '*.pyo' \
            \) -exec rm -rf {} + 2>/dev/null || true
    fi
    echo "Cleanup complete."
}

TYPE=""; DMG=0; CLEAN=0; FULL=0
for a in "$@"; do
    case "$(echo "$a" | tr A-Z a-z)" in
        debug)       TYPE=Debug ;;
        release)     TYPE=Release ;;
        full)        FULL=1 ;;
        dmg)         DMG=1 ;;
        clean|clear) CLEAN=1 ;;
        -h|--help|help) usage ;;
        *)           usage ;;
    esac
done

if (( CLEAN )) && [[ -z "$TYPE" ]]; then
    clean_all
    exit 0
fi

[[ -z "$TYPE" ]] && TYPE="Debug"

NR_FLAGS=""
(( FULL )) || NR_FLAGS="-DNR_AWS=OFF -DNR_SMB=OFF -DNR_NFS=OFF"

# Brew bootstrap.  Checking only cmake/ninja/pkg-config leaves a partially
# installed Brewfile (notably wxwidgets) to fail later during CMake configure.
command -v brew >/dev/null 2>&1 || { echo "Homebrew required: https://brew.sh/" >&2; exit 1; }
if ! brew bundle check --file="$SD/Brewfile" >/dev/null 2>&1; then
    echo "Installing missing Homebrew dependencies from Brewfile..."
    brew bundle --file="$SD/Brewfile"
    brew link libarchive --force >/dev/null 2>&1 || true
fi

# Wipe stale in-source CMake state (from accidental `cmake .` at repo root)
if [[ -f "$SD/CMakeCache.txt" ]]; then
    echo "Cleaning stale in-source CMake state at repo root..."
    (cd "$SD" && find . \
        \( -path ./_debug -o -path ./_release -o -path ./.git -o -path ./.cache \) -prune -o \
        \( -name CMakeCache.txt -o -name CMakeFiles -o -name cmake_install.cmake -o -name CTestTestfile.cmake \) -print0 \
        | xargs -0 rm -rf) || true
fi

DIR="_$(echo "$TYPE" | tr A-Z a-z)"
(( CLEAN )) && rm -rf "$DIR"

# Drop dir if a prior run used a non-Ninja generator
if [[ -f "$DIR/CMakeCache.txt" ]] && ! grep -q "^CMAKE_GENERATOR:INTERNAL=Ninja$" "$DIR/CMakeCache.txt"; then
    echo "Generator changed, recreating $DIR..."
    rm -rf "$DIR"
fi

mkdir -p "$DIR" && cd "$DIR"

# Always configure: a failed CMake run leaves CMakeCache.txt behind, but no
# usable build graph.  Reconfiguring is also how CMake picks up source edits.
cmake -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" \
    -DPYTHON=yes \
    -DCMAKE_PREFIX_PATH=/opt/homebrew \
    -DCMAKE_INSTALL_RPATH=/opt/homebrew/lib \
    -DCMAKE_BUILD_RPATH=/opt/homebrew/lib \
    -DCMAKE_INSTALL_PREFIX=install \
    $NR_FLAGS ..
ninja

(( DMG )) || exit 0

ninja install

FIXIN="$SD/packaging/osx/FixupBundle.cmake.in"
ICU_LIB="$(brew --prefix icu4c 2>/dev/null)/lib"
ICU_IGN="libicudata.78.dylib;libicuuc.78.dylib;libicui18n.78.dylib;libicuio.78.dylib;libicutu.78.dylib;libicutest.78.dylib"
if [[ -f "$FIXIN" ]]; then
    sed -e 's/@APP_NAME@/far2l/g' \
        -e 's|"" IGNORE_ITEM "python;python3;python3.8;Python;.Python")|"${DIRS}" IGNORE_ITEM "python;python3;python3.8;Python;.Python;'"$ICU_IGN"'")|' \
        -e 's|^fixup_bundle|set(DIRS "/opt/homebrew/lib" "'"$ICU_LIB"'")\
fixup_bundle|' \
        -e 's|/usr/bin/codesign -s |/usr/bin/codesign --force -s |' \
        "$FIXIN" > packaging/osx/FixupBundle.cmake
fi

# Pre-seed Frameworks/ with ICU dylibs, rewriting @loader_path → absolute paths
FW="$PWD/install/far2l.app/Contents/Frameworks"
mkdir -p "$FW"
cp -n "$ICU_LIB"/libicu*.78.dylib "$FW/" 2>/dev/null || true
for lib in "$FW"/libicu*.78.dylib; do
    for dep in "$FW"/libicu*.78.dylib; do
        install_name_tool -change "@loader_path/$(basename "$dep")" "$dep" "$lib" 2>/dev/null || true
    done
done

find install/far2l.app -name CMakeFiles -type d -exec rm -rf {} + 2>/dev/null || true

FAR2L_CODESIGN_CERT=- cpack -G DragNDrop 2> >(grep -v "warning: changes being made to the file will invalidate the code signature" >&2)
