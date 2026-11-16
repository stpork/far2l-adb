Run from the repository root:

```sh
python3 -B plugins/adb/tests/pty_status.py
python3 -B plugins/adb/tests/replacement.py
```

ASan/UBSan checks exercise production child-process handling with injected PTY,
executable and wait failures, EINTR, and independent caller threads. Replacement
tests execute the production save/restore shell commands against temporary local
files and directories: stale backups, failed uploads, cancellation, partial
moves, quoted filenames, exceptions and successful cleanup. No adb executable or
device is required. Live-device USB/connection behavior still needs a device.
