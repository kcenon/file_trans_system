# macOS errno / ThreadSanitizer reproduction

The 2026-10-15 follow-up isolated the historical Asio `clear_last_error`,
`get_last_error` and `config_get` reports to access to Darwin's `errno` storage.
Those source locations assign or read `errno`. A standalone program with **no
project, Asio, thread_system, logger_system or network_system code** reproduces a
ThreadSanitizer report on a write to `errno` by one thread after a different
thread using the same address has finished.

`tools/repro_errno_thread_reuse.cpp` launches eight independent parent threads,
each creating and joining 500 children. Children access only their own `errno`.
The program records each address and its access interval in a separate array
entry. Relaxed atomic sequence numbers order observations without adding an
acquire/release synchronization edge between the errno accesses.

| Toolchain / host | Observations | Reused addresses | Overlapping recorded intervals | TSAN outcome |
| --- | ---: | ---: | ---: | --- |
| Apple Clang 21.0.0 (`clang-2100.3.34.2`), macOS 27.2 ARM64 | 4,000 | 16 | 0 | One warning; process exits 134 |
| Homebrew LLVM 23.1.1, same host | 4,000 | 12 | 0 | Same errno-write warning; exits 134 |

The Apple run had 17 distinct addresses; the reported reused address was
`0x00016db330ac`. The log identifies the previous writer as a **finished** thread.
An earlier reduction including `getaddrinfo` also reported errno reuse: 4,000
observations, 11 addresses, nine reused addresses, and zero overlapping access
intervals. Removing all networking calls preserved the warning.

This establishes a standalone platform/runtime reporting defect for that errno
pattern. It does **not** prove that every warning in the full project is benign.
The full project TSAN command remains unsuppressed, and its actual exit code and
any other findings must be reported separately. Neither this reproducer nor
focused passing tests satisfy an all-green sanitizer acceptance gate.

Reproduce with either installed compiler:

```sh
clang++ -std=c++20 -fsanitize=thread -g -O1 \
  tools/repro_errno_thread_reuse.cpp -o /tmp/fts-errno-reuse
/tmp/fts-errno-reuse > /tmp/fts-errno-reuse.csv 2> /tmp/fts-errno-reuse.log
```

CSV columns are `errno_address,begin_sequence,end_sequence`. Group by address,
sort intervals by beginning sequence, and verify that each end is smaller than
the next beginning. The executable's nonzero exit is expected on the affected
runtime; no suppression or `no_sanitize` annotation is used.

Full local artifacts are retained under
`/Users/dongcheolshin/Sources/file-trans-system-followup-20261015/logs/`:
`errno-only.csv`, `errno-only-tsan.log`, `errno-only-summary.json`,
`errno-llvm.csv`, `errno-llvm-tsan.log`, and the earlier `errno-reuse-*` files.

## Final project rerun

After repairing the separate cross-instance logger formatting race, the fresh
fully instrumented project rerun executed 1,064 unit and 160 integration tests
with zero assertion failures and zero skips; all four process restart scenarios
also passed. Integration ran for 293.8 seconds and exited with SIGABRT (`-6` in
Python, shell status 134): two `socket_ops::clear_last_error` warnings and one
`config_get` warning. Each identifies a previous thread as finished, matching
the isolated errno address-reuse pattern. No logger formatting warning remains.
The driver returns 1, so the full unsuppressed TSAN gate is still **failing**.

Artifacts: `logs/189-tsan-corrected/summary.json`, `integration.log`,
`warning-summary.json`, and `process-resume.log` in the follow-up evidence root.
The focused concurrent-logger/observability test also passed with
`TSAN_OPTIONS=halt_on_error=1`. That subset does not override the full gate.
