"""Exercise real Mach-O/dSYM preparation with the macOS toolchain."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_symbols", ROOT / "packaging/osx/prepare_symbols.py")
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)


@unittest.skipUnless(sys.platform == "darwin", "requires the macOS Mach-O toolchain")
class PackageSymbolsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="far2l-symbol-tests-")
        cls.root = Path(cls.temporary.name)
        source = cls.root / "probe.cpp"
        source.write_text('''
#include <cstdio>
static __attribute__((noinline)) void private_crash_marker() { std::puts("marker"); }
int main() { private_crash_marker(); }
''')
        cls.debug_binary = cls.compile(source, "debug", ["-O0", "-g"])
        cls.release_binary = cls.compile(source, "release", ["-O2", "-g"])
        cls.no_debug_binary = cls.compile(source, "no-debug", ["-O2"])

    @classmethod
    def compile(cls, source, name, flags):
        obj = cls.root / (name + ".o")
        binary = cls.root / name
        subprocess.run(["clang++", *flags, "-c", str(source), "-o", str(obj)], check=True)
        subprocess.run(["clang++", str(obj), "-o", str(binary)], check=True)
        return binary

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(dir=self.root))
        self.app = self.case / "far2l.app"
        self.binary = self.app / "Contents/MacOS/far2l"
        self.binary.parent.mkdir(parents=True)
        self.symbols = self.case / "symbols"

    def install(self, binary):
        shutil.copy2(binary, self.binary)

    def manifest(self):
        return json.loads(next(self.symbols.glob("far2l-*.json")).read_text())

    def test_release_preserves_local_names_and_matching_dwarf(self):
        self.install(self.release_binary)
        before = packaging.uuids(self.binary)
        names = packaging.run("nm", "-an", str(self.binary))
        marker = next(line for line in names.splitlines() if "private_crash_marker" in line)
        packaging.prepare(self.app, self.symbols, "Release", ROOT)
        self.assertEqual(before, packaging.uuids(self.binary))
        self.assertIn(marker, packaging.run("nm", "-an", str(self.binary)))
        image = self.manifest()["images"][0]
        dsym = self.symbols / image["dsym"]
        packaging.validate_dsym(dsym, before)
        # The archive can restore the exact source line, not just an exported name.
        address = marker.split()[0]
        dwarf = dsym / "Contents/Resources/DWARF/far2l"
        resolved = packaging.run("atos", "-arch", "arm64" if "arm64" in before[0][1] else "x86_64",
                                 "-o", str(dwarf), "0x" + address)
        self.assertIn("probe.cpp:", resolved)
        self.assertFalse(list(self.app.rglob("*.dSYM")))

    def test_debug_copy_is_byte_identical(self):
        self.install(self.debug_binary)
        before = self.binary.read_bytes()
        packaging.prepare(self.app, self.symbols, "Debug", ROOT)
        self.assertEqual(before, self.binary.read_bytes())
        self.assertIsNotNone(self.manifest()["images"][0]["dsym"])

    def test_cached_archive_is_reused_and_old_uuid_retained(self):
        self.install(self.release_binary)
        packaging.prepare(self.app, self.symbols, "Release", ROOT)
        old = self.symbols / self.manifest()["images"][0]["dsym"]
        timestamp = old.stat().st_mtime_ns
        self.install(self.release_binary)
        packaging.prepare(self.app, self.symbols, "Release", ROOT)
        self.assertEqual(timestamp, old.stat().st_mtime_ns)
        self.install(self.debug_binary)
        packaging.prepare(self.app, self.symbols, "Debug", ROOT)
        self.assertTrue(old.is_dir())
        self.assertEqual(len(list(self.symbols.glob("*/*.dSYM"))), 2)

    def test_no_debug_info_fails_before_stripping(self):
        self.install(self.no_debug_binary)
        before = self.binary.read_bytes()
        with self.assertRaisesRegex(RuntimeError, "No DWARF"):
            packaging.prepare(self.app, self.symbols, "Release", ROOT)
        self.assertEqual(before, self.binary.read_bytes())

    def test_uuid_mismatch_is_rejected(self):
        self.install(self.release_binary)
        packaging.prepare(self.app, self.symbols, "Release", ROOT)
        dsym = self.symbols / self.manifest()["images"][0]["dsym"]
        with self.assertRaisesRegex(RuntimeError, "UUID mismatch"):
            packaging.validate_dsym(dsym, packaging.uuids(self.debug_binary))

    def test_symbol_archive_inside_app_is_rejected(self):
        self.install(self.release_binary)
        with self.assertRaisesRegex(RuntimeError, "outside the .app"):
            packaging.prepare(self.app, self.app / "symbols", "Release", ROOT)

    def test_source_archive_without_git_metadata(self):
        self.install(self.release_binary)
        packaging.prepare(self.app, self.symbols, "Release", self.case)
        self.assertIsNone(self.manifest()["git_revision"])
        self.assertIsNone(self.manifest()["git_dirty"])


if __name__ == "__main__":
    unittest.main()
