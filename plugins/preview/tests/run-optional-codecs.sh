#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 3 ]]; then
    echo "Usage: $0 image.png image.heic video.mp4" >&2
    exit 2
fi
repo=$(cd "$(dirname "$0")/../../.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
sources=("$repo/plugins/preview/tests/optional-codecs.cpp"
    "$repo/plugins/preview/src/Image.cpp"
    "$repo/plugins/preview/src/decoder/ImageDecoder_stb.cpp"
    "$repo/plugins/preview/src/decoder/ImageDecoder_common.cpp")
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined)
# pkg-config emits compiler flags, not filenames.
read -r -a heif_flags <<< "$(pkg-config --cflags libheif)"
priorities=(0)
if [[ $(uname -s) == Darwin ]]; then
    flags+=(-DPREVIEW_HAS_NATIVE=1 -framework Accelerate -framework ImageIO
        -framework CoreGraphics -framework CoreFoundation -framework AVFoundation
        -framework Foundation -framework CoreMedia)
    sources+=("$repo/plugins/preview/src/decoder/ImageDecoder_mac.cpp"
        "$repo/plugins/preview/src/decoder/VideoDecoder_mac.mm"
        "$repo/plugins/preview/src/decoder/VideoDecoder_common.cpp")
    priorities+=(1)
else
    flags+=(-ldl -pthread)
fi
for priority in "${priorities[@]}"; do
    "${CXX:-c++}" "${sources[@]}" "${flags[@]}" "${heif_flags[@]}" \
        -DPREVIEW_NATIVE_DEFAULT="$priority" -o "$test_dir/test"
    "$test_dir/test" "$1" "$2" "$3"
done
