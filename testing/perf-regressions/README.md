# Performance-report regression checks

Run from any directory with Python 3 and a C++17 compiler supporting AddressSanitizer
and UndefinedBehaviorSanitizer:

```sh
python3 -B testing/perf-regressions/run.py
```

`CXX` selects the compiler executable (default: `clang++`). Each check compiles
current production excerpts or headers in a temporary directory. Application state
and selected I/O calls are explicitly stubbed; these are focused regression tests,
not a replacement for application or platform integration tests. An extraction
failure after source restructuring requires updating the test's section anchors.

## Fix coverage

The original report's B/P/M identifiers and the validation report's adjacent A
identifiers map to these independently committed fixes:

| Finding | Fix | Check |
| --- | --- | --- |
| A14 | Recheck the source after copying | Existing `0002-copy-files` smoke test corrected |
| A4 | Reject February 29 outside leap years | `time.py` |
| P5/A3 | Checked integer FILETIME conversion retaining 100 ns precision | `time.py` |
| A7 | Clear wrapped viewport offset when removing its top line | `editor.py` |
| B1/A1 | Bound iTerm2 frames, fields and UTF-8 decoding | `tty_parser.py` |
| B4 | Preserve filename dots and normalize path components | `paths.py` |
| B5/A2 | Decode UTF-16 by file order and validate surrogate pairs | `viewer_utf16.py` |
| M5/A8 | Reset viewer cache ownership and per-file state on reopen | `cache.py` |
| A5 | Initialize plugin panel payloads, including Unix mode | `plugin_item.py` |
| A6 | Return equality for identical sort keys and parent entries | `sort_order.py` |
| P1 | Borrow comparison payloads without name allocations | `plugin_sort.py` |
| A11 | Correct padded short-write offset accounting | `copy_short_write.py` |
| A12 | Use buffered destinations for append/resume | `copy_alignment.py` |
| A10 | Remember unsupported Linux fast-copy fallback per transfer | `copy_fallback.py` |
| A9 | Fall back on existing macOS clone destinations; bypass append/resume | `copy_clone.py` |
| A13 | Probe larger chunks when fixed latency prevents growth | `copy_sizing.py` |
| M3 | Bound interned composite sequences without invalidating existing IDs | `composite.py` |
| P4 | Retain dirty rows and batch adjacent changed TTY cells | `tty_output.py` |

The composite registry limits new registrations to 65,536 entries, 16 MiB of
string payload, and 1,024 code points per sequence. At a limit, new sequences
display their base character; existing IDs and lookup pointers remain valid.
Container overhead is additionally bounded by the entry limit, not included in
the payload limit. This trades new-cluster fidelity under exhaustion for bounded
retention without unsafe eviction.

## Validation limits

All 16 checks passed on macOS arm64 with ASan/UBSan. The macOS clone check calls
the native API; the Linux fast-copy and direct-I/O checks inject syscall results
or inspect the production flags path. They do not establish behavior on a real
Linux filesystem. The buffer controller uses deterministic latency models, not
remote-storage throughput measurements. TTY tests count copied cells and output
calls; they do not measure SSH latency or validate a terminal emulator's complete
wide-character rendering behavior.

The full macOS Debug application and configured plugins built with Ninja and
Homebrew dependencies (`NR_AWS`, `NR_SMB`, and `NR_NFS` disabled). The existing Go
smoke suite was not run because Go is unavailable here. A native PTY smoke check
opened a UTF-16BE BOM file, observed the expected BMP and supplementary-plane
characters in terminal output, and exited successfully with F10 (`--mortal` mode).
Real Linux O_DIRECT and
filesystem-pair copy tests, copy cancellation/retry/metadata integration, wrapped
editor UI/undo scenarios, and SSH/network performance measurements remain
platform-integration follow-ups.

Rejected UAF claims B2/B3 require no patch. P2 directory-relative metadata and sort
algorithm changes, M1/M2/M4 memory-layout redesigns, and M5 mmap are unproven
optimization candidates. No fast-copy default or `tcdrain` policy was changed.
