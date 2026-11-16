"""Exercise build.sh cleanup against disposable repositories and command stubs."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildCleanupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='far2l-build-clean-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.repo = self.directory / 'repo'
        self.caller = self.directory / 'other-project'
        self.repo.mkdir()
        self.caller.mkdir()
        shutil.copy2(ROOT / 'build.sh', self.repo / 'build.sh')

    def test_clear_preserves_sources_backups_and_symbols(self):
        subprocess.run(['git', 'init', '-q', str(self.repo)], check=True)
        subprocess.run(['git', '-C', str(self.repo), 'add', 'build.sh'], check=True)
        for name in ['new-source.cpp', 'photo.png', 'work.bak', 'work.orig',
                     '.symbols/saved.dSYM', 'cache/__pycache__/test.pyc', '_debug/file.o']:
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('keep user files; remove generated files')
        subprocess.run(['/bin/bash', str(self.repo / 'build.sh'), 'clear'],
                       cwd=self.caller, check=True, capture_output=True)
        for name in ['new-source.cpp', 'photo.png', 'work.bak', 'work.orig', '.symbols/saved.dSYM']:
            self.assertTrue((self.repo / name).is_file(), name)
        self.assertFalse((self.repo / '_debug').exists())
        self.assertFalse((self.repo / 'cache/__pycache__').exists())

    def test_failed_build_directory_creation_stops_before_cmake(self):
        (self.repo / '_release').write_text('a file blocks the build directory')
        bins = self.directory / 'bin'
        bins.mkdir()
        trace = self.directory / 'cmake-called'
        for name, content in [('brew', 'exit 0'), ('cmake', 'touch "$CHECK_CMAKE_TRACE"; exit 2')]:
            executable = bins / name
            executable.write_text('#!/bin/sh\n' + content + '\n')
            executable.chmod(0o755)
        env = dict(os.environ, PATH=str(bins) + ':/usr/bin:/bin', CHECK_CMAKE_TRACE=str(trace))
        result = subprocess.run(['/bin/bash', str(self.repo / 'build.sh'), 'release'],
                                cwd=self.caller, env=env, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(trace.exists())

    def test_release_clean_is_relative_to_script(self):
        for parent in [self.repo, self.caller]:
            (parent / '_release').mkdir()
            (parent / '_release/valuable').write_text('data')
        bins = self.directory / 'bin'
        bins.mkdir()
        for name, content in [
            ('brew', 'exit 0'),
            ('cmake', 'printf "%s\\n" "$PWD" "$@" > "$CHECK_CMAKE_TRACE"; exit 2')]:
            executable = bins / name
            executable.write_text('#!/bin/sh\n' + content + '\n')
            executable.chmod(0o755)
        trace = self.directory / 'cmake.txt'
        env = dict(os.environ, PATH=str(bins) + ':/usr/bin:/bin', CHECK_CMAKE_TRACE=str(trace))
        result = subprocess.run(['/bin/bash', str(self.repo / 'build.sh'), 'release', 'clean'],
                                cwd=self.caller, env=env, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertTrue((self.caller / '_release/valuable').is_file())
        self.assertFalse((self.repo / '_release/valuable').exists())
        lines = trace.read_text().splitlines()
        self.assertEqual(Path(lines[0]).resolve(), (self.repo / '_release').resolve())
        self.assertEqual(Path(lines[-1]).resolve(), self.repo.resolve())


if __name__ == '__main__':
    unittest.main()
