Run from the repository root:

```sh
for test in plugins/NetRocks/tests/*.py; do python3 -B "$test" || exit; done
```

Six ASan/UBSan checks cover allocation fallback/growth failures, failed symlink
creation, relocated absolute link targets, retry progress, same-file protection,
local hardlink/symlink aliases, dangling links, and local/SFTP close failures.
They compile the production buffer, transfer helpers and I/O implementations
with explicit filesystem, allocation and protocol fault injection. No server or
user files are touched. The shared compiler runner is in
`testing/perf-regressions/common.py`.
