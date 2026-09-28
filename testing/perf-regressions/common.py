"""Compile current production excerpts with explicit test stubs and sanitizers."""
from pathlib import Path
import os
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]

def section(path, start, end):
    text = (ROOT / path).read_text()
    begin = text.index(start)
    return text[begin:text.index(end, begin)]

def check(name, source, args=()):
    with tempfile.TemporaryDirectory(prefix='far2l-regression-') as tmp:
        d=Path(tmp); cpp=d/'test.cpp'; exe=d/'test'
        cpp.write_text(source)
        subprocess.run([os.environ.get('CXX','clang++'), '-std=c++17', '-g', '-O1',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I'+str(ROOT), '-I'+str(ROOT/'WinPort'), '-I'+str(ROOT/'utils/include'),
                        str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe), tmp, *args], check=True, timeout=30)
        print(name+': PASS', flush=True)
