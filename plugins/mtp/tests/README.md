Run `python3 -B plugins/mtp/tests/transfers.py` from the repository root.

The ASan/UBSan check executes the production copy/move work unit, recursive upload
and replacement transaction with a small in-memory backend. It verifies that an
unsupported device-side copy preserves the old destination for host fallback,
that unsupported local types, lstat/readdir failures and cancellation fail the
whole upload, and that failed backup creation never starts an overwrite. It also
checks rollback, cleanup failure and successful replacement. Only temporary local
fixtures are used; real MTP firmware/USB behavior still needs device validation.
