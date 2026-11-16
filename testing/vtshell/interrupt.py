#!/usr/bin/env python3
"""Exercise far2l's completion prompt in real interactive shell PTYs."""
from pathlib import Path
import errno
import os
import pty
import select
import shutil
import signal
import re
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


pending = {}


def read_until(fd, expected, timeout=10):
    output = pending.pop(fd, bytearray())
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        match = re.search(expected, output)
        if match:
            pending[fd] = output[match.end():]
            return bytes(output[:match.end()])
        if select.select([fd], [], [], min(.1, max(0, deadline-time.monotonic())))[0]:
            try:
                data = os.read(fd, 65536)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not data:
                break
            output.extend(data)
    raise AssertionError(f'Missing {expected!r} in {bytes(output)!r}')


def exercise(shell, argv, generator, directory):
    pid, fd = pty.fork()
    if pid == 0:
        # Do not load user prompt plugins or shell configuration in a regression.
        os.environ['TERM'] = 'dumb'
        os.execv(shell, [shell, *argv])
    try:
        os.write(fd, b"printf '__READY_%s__\\n' 'PTY'\n")
        read_until(fd, b'__READY_PTY__')
        cases = [
                ('true', False, 0), ('false', False, 1),
                ('(exit 42)', False, 42),
                ("while sleep 1; do :; done", True, 130),
                ('true', False, 0),
                ('sleep 30', True, 130), ('false', False, 1)]
        if Path(shell).name == 'zsh':
            cases += [
                ('unsetopt promptpercent; while sleep 1; do :; done', True, 1),
                ('setopt promptpercent; while sleep 1; do :; done', True, 1),
                ('unsetopt promptpercent promptsubst; sleep 30', True, 1),
                ('setopt promptpercent promptsubst; sleep 30', True, 1),
                ('true', False, 0)]
        for index, (command, interrupted, status) in enumerate(cases):
            marker = f'pty{index}:'
            commands = subprocess.check_output([str(generator), marker], text=True)
            script = directory / 'command.sh'
            script.write_text(commands + "printf '__START_%s__\\n' 'COMMAND'\n" +
                              'FARVTRESULT=1\n' + command + '\nFARVTRESULT=$?\n')
            # Same sourcing and explicit completion tail as ExecuteCommandBegin.
            line = (f" . '{script}'; printf '\\033_far2l_%s%s\\007' "
                    f"'p' 'ty{index}:'\"$FARVTRESULT\";PS1=''\n")
            os.write(fd, line.encode())
            started = read_until(fd, b'__START_COMMAND__')
            assert b'too many arguments' not in started, started
            if interrupted:
                # Wait for the external command to enter the foreground pgrp.
                time.sleep(.15)
                os.write(fd, b'\x03')
            result = b'[1-9][0-9]*' if interrupted else str(status).encode()
            read_until(fd, re.escape(f'\x1b_far2l_{marker}'.encode()) + result + b'\x07')
        print(f'{Path(shell).name}: normal, failed, interrupted and repeated commands PASS')
    finally:
        os.close(fd)
        try:
            os.kill(pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
        os.waitpid(pid, 0)


def main():
    source = (ROOT / 'far2l/src/vt/vtshell_compose.cpp').read_text()
    begin = source.index('std::string VT_ComposeMarkerCommand(')
    end = source.index('static std::string VT_ComposeInitialTitleCommand(', begin)
    with tempfile.TemporaryDirectory(prefix='far2l-vtshell-') as tmp:
        directory = Path(tmp)
        cpp = directory / 'prompt.cpp'
        cpp.write_text('#include <string>\n#include <iostream>\n' + source[begin:end] +
                       '\nint main(int argc, char **argv) { std::cout << VT_ComposePromptCommand(argv[1]); }\n')
        generator = directory / 'prompt'
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                        str(cpp), '-o', str(generator)], check=True)
        tested = 0
        for name, argv in [('zsh', ['-f', '-i']), ('bash', ['--noprofile', '--norc', '-i']),
                           ('dash', ['-i'])]:
            shell = shutil.which(name)
            if shell:
                exercise(shell, argv, generator, directory)
                tested += 1
            else:
                print(f'{name}: SKIP (not installed)')
        assert tested, 'No supported shells installed'


if __name__ == '__main__':
    main()
