"""Publish native clipboard transactions to an isolated pasteboard."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class NativeClipboard(unittest.TestCase):
    def test_plain_mixed_and_cancelled_transactions(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp, 'test.mm')
            source.write_text(r'''
#import "WinPort/src/Backend/WX/Mac/pasteboard.mm"
#include <cassert>
int main() { @autoreleasepool {
    NSPasteboard *board = [NSPasteboard pasteboardWithUniqueName];
    assert(StagePasteboardText("abc!", 3));
    assert(PublishTo(board));
    assert([[board stringForType:NSPasteboardTypeString] isEqual:@"abc"]);
    assert(StagePasteboardData(CFSTR("public.html"), "<i/>", 4));
    assert(StagePasteboardText(L"л🌍!", 2));
    assert(StagePasteboardData(CFSTR("org.far2l.test"), "\0x", 2));
    assert(PublishTo(board));
    assert([[board stringForType:NSPasteboardTypeString] isEqual:@"л🌍"]);
    assert([[board dataForType:NSPasteboardTypeHTML] length] == 4);
    assert([[board dataForType:@"org.far2l.test"] length] == 2);
    assert(StagePasteboardText("drop", 4));
    DiscardPasteboardData();
    assert(PublishTo(board));
    assert([[board stringForType:NSPasteboardTypeString] isEqual:@"л🌍"]);
    [board releaseGlobally];
} }
''')
            exe = pathlib.Path(tmp, 'test')
            for ownership in ('-fobjc-arc', '-fno-objc-arc'):
                subprocess.run(['clang++', '-std=c++17', ownership, '-Werror',
                            '-I', str(ROOT), str(source), '-framework', 'AppKit',
                            '-framework', 'Foundation', '-o', str(exe)], check=True)
                subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
