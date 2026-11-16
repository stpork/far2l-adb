from pathlib import Path
import os
import platform
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='preview-regression-') as tmp:
    directory = Path(tmp)
    # Compile the actual fallback decoder with isolated codec dependencies.
    source = (ROOT / 'plugins/preview/src/decoder/ImageDecoder.cpp').read_text()
    begin = source.index('namespace {\nclass FallbackImageDecoder')
    end = source.index('std::vector<std::shared_ptr<ImageDecoder>> DecoderFactory::CreateDecoders()', begin)
    (directory/'fallback.inc').write_text(source[begin:end])
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png = directory/'test.png'
    png.write_bytes(b'\x89PNG\r\n\x1a\n' +
                    chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 2, 8, 2, 0, 0, 0)) +
                    chunk(b'IDAT', zlib.compress((b'\0' + bytes(range(12))) * 2)) + chunk(b'IEND', b''))
    large_png = directory/'large.png'
    large_png.write_bytes(b'\x89PNG\r\n\x1a\n' +
                          chunk(b'IHDR', struct.pack('>IIBBBBB', 256, 128, 8, 2, 0, 0, 0)) +
                          chunk(b'IDAT', zlib.compress((b'\0' + bytes(range(256)) * 3) * 128)) +
                          chunk(b'IEND', b''))
    sources = ['plugins/preview/tests/image-safety.cpp', 'plugins/preview/src/Image.cpp',
               'plugins/preview/src/decoder/ImageDecoder_stb.cpp',
               'plugins/preview/src/decoder/ImageDecoder_common.cpp']
    flags = []
    if platform.system() == 'Darwin':
        sources += ['plugins/preview/src/decoder/ImageDecoder_mac.cpp']
        for framework in ['Accelerate', 'ImageIO', 'CoreGraphics', 'CoreFoundation']:
            flags += ['-framework', framework]
    else:
        flags += ['-pthread']
    executable = directory/'test'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I'+str(directory),
                    *[str(ROOT/path) for path in sources], *flags, '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(png), str(large_png)], check=True, timeout=60)
    print('Preview allocation failures, RGB/RGBA transforms and available PNG decoders: PASS')
