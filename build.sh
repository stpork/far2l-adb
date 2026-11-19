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
  dmg            Also package a macOS DragNDrop DMG (a standalone .app is always prepared)
  clean|clear    When used alone, remove recognized out-of-source build trees.
                 When used with debug/release, clean the build directory first.
EOF
    exit 1
}

is_build_tree() {
    [[ -d "$1" && ! -L "$1" && -d "$1/CMakeFiles" && -f "$1/CMakeCache.txt" ]] &&
        grep -Fxq "CMAKE_HOME_DIRECTORY:INTERNAL=$SD" "$1/CMakeCache.txt" &&
        grep -Fxq "CMAKE_CACHEFILE_DIR:INTERNAL=$1" "$1/CMakeCache.txt"
}

clean_all() {
    for dir in "$SD"/_debug "$SD"/_release "$SD"/_build "$SD"/build "$SD"/cmake-build-*; do
        if is_build_tree "$dir"; then
            echo "Removing build tree $dir"
            rm -rf -- "$dir"
        fi
    done
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

# In-source files can belong to a different build. Leave them in place.
if [[ -f "$SD/CMakeCache.txt" ]]; then
    echo "In-source CMake cache found at $SD/CMakeCache.txt." >&2
    exit 1
fi

DIR="$SD/_$(echo "$TYPE" | tr A-Z a-z)"
if (( CLEAN )) && [[ -e "$DIR" || -L "$DIR" ]]; then
    is_build_tree "$DIR" || { echo "Unrecognized build directory: $DIR" >&2; exit 1; }
    rm -rf -- "$DIR"
fi

if [[ -f "$DIR/CMakeCache.txt" ]] && ! grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja$' "$DIR/CMakeCache.txt"; then
    is_build_tree "$DIR" || { echo "Unrecognized build directory: $DIR" >&2; exit 1; }
    echo "Generator changed, recreating $DIR..."
    rm -rf -- "$DIR"
fi

mkdir -p "$DIR"
cd "$DIR"

# Always configure: a failed CMake run leaves CMakeCache.txt behind, but no
# usable build graph.  Reconfiguring is also how CMake picks up source edits.
cmake -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" \
    -DPYTHON=yes \
    -DCMAKE_PREFIX_PATH=/opt/homebrew \
    -DCMAKE_INSTALL_RPATH=/opt/homebrew/lib \
    -DCMAKE_BUILD_RPATH=/opt/homebrew/lib \
    -DCMAKE_INSTALL_PREFIX=install \
    -DFAR2L_SYMBOLS_DIR="${FAR2L_SYMBOLS_DIR:-$SD/.symbols}" \
    $NR_FLAGS "$SD"
ninja

# Package a copy: preserve the original debug map and object files for dSYM generation.
rm -rf "$PWD/package"
export FAR2L_CODESIGN_CERT="${FAR2L_CODESIGN_CERT:--}"
cmake --install . --component app --prefix "$PWD/package" --config "$TYPE"
echo "Application: $PWD/package/far2l.app"
echo "Debug symbols: ${FAR2L_SYMBOLS_DIR:-$SD/.symbols}"

if (( DMG )); then
    cpack -G DragNDrop -C "$TYPE"
fi
