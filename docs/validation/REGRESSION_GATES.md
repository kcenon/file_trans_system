# Regression gates — issue #189

The tables below preserve the 2026-10-13 evidence. Current follow-up results,
including fully instrumented sanitizer dependencies and new cloud/adapter
regressions, are in the [2026-10-15 report](FOLLOWUP_176_190.md). The former
unresolved errno pattern now has a [standalone reproducer](TSAN_ERRNO_REPRODUCER.md);
the full unsuppressed TSAN outcome is reported separately.

The audit baseline is 0608737c65ca2a270d9b27e1dcacbf43aff8168d. Results below
were executed locally on macOS arm64 with Apple Clang 21 on 2026-10-13.
Workflow definitions are configured, not evidence of a completed GitHub run.

## Reproduce

```sh
python3 tools/bootstrap_dependencies.py --jobs 6
python3 tools/ci_build.py --build build
python3 tools/run_ci_tests.py --build build --artifacts artifacts/tests
python3 tools/check_regression_mutations.py --build build --artifacts artifacts/mutations
python3 tools/run_minio_tests.py --test-binary build/bin/file_trans_cloud_integration_tests --artifacts artifacts/minio
```

Use the scripts' --help for installed MinIO, sanitizer, crypto-disabled, and
coverage configurations. All dependency commits are pinned in
cmake/dependency-revisions.json. Checked-in patches fix TCP close draining,
long-lived I/O jobs starving the fixed worker pool (100 connections), and a
shared stream formatting race. The bootstrap checks patches before applying.

## Executed evidence (2026-10-13)

| Check | Result |
|---|---|
| Debug component suite | 1,048 passed |
| Encryption disabled | 819 component + 136 integration + process restart passed |
| Debug public integration | 152 passed; exact bytes/SHA-256 and terminal outcomes |
| Crash/restart | Four upload/download × plain/encrypted process cases passed |
| Local MinIO | 32 passed, zero skipped; real signed HTTP and multipart/range I/O |
| Mutation proof | Three mutations × three targeted assertions failed as required; originals restored |
| AddressSanitizer | 1,048 component + 152 integration + process restart passed |
| UndefinedBehaviorSanitizer | 1,048 component + 151 integration passed initially; corrected 100 MiB case passed focused retry (45.9 s); process restart passed |
| Fully instrumented ThreadSanitizer | 1,048 component passed; 100 MiB retry passed (175.8 s); integration race gate remains failing (see below) |
| Whole production line coverage | **63.85% (12,986 / 20,338), FAIL against 80%** |
| Live GCS | Unavailable: explicit service account and test object not supplied |
| QUIC enabled | Dependency handshake/cleanup blocker; see ../protocol/TRANSPORTS.md |
| Windows and remote CI | Not executed; no current Windows validation claim |

Local ASAN/UBSAN runs instrument the project; the TSAN retry also instruments
thread_system, logger_system and network_system. CI sanitizer jobs build all
three dependencies with the selected sanitizer. System libraries remain
outside this instrumentation. Linux ASAN disables leak detection because the
pinned network dependency intentionally retains global I/O contexts; this is
an address-safety check, not proof of leak freedom.

The fully instrumented macOS integration run reports two races with Asio
socket_ops::clear_last_error / get_last_error and config_get stacks during
ConcurrentStressTest.RapidConnectDisconnectConcurrent. At that pass their root cause was
unresolved; they were not classified as false positives and no race suppression
is installed. Keep the sanitizer acceptance item open. Reproduce with:

```sh
python3 tools/bootstrap_dependencies.py --build-directory build-tsan --sanitizer thread
python3 tools/ci_build.py --build build-tsan --sanitizer TSAN --dependency-build build-tsan
build-tsan/bin/file_trans_integration_tests --gtest_filter=ConcurrentStressTest.RapidConnectDisconnectConcurrent
```

A 100 MiB upload also exposed a test-specific three-second protocol response
deadline that was insufficient for final SHA-256 verification under
instrumentation. That workload now allows 30 seconds per response and 180
seconds overall; small-message fault tests retain short deadlines.

## Coverage scope and failure policy

coverage_gate.py links the inventory to the **complete production archive** and
all test binaries, so untouched production translation units stay in the
denominator. There are no production path exclusions. Every src/*.cpp must
have an executable mapping, except the empty namespace-only file_transfer.cpp.
The inventory records headers with no emitted executable mapping. The active
build enables encryption and LZ4 and disables the blocked QUIC dependency path;
compiled-out branches cannot be measured by this build. Coverage is therefore
not a claim about every preprocessor configuration.

The full unit, integration and MinIO suites produced the linked
[coverage summary](coverage-20261013.json). Missing profiles, missing production
mappings, nonzero-CFG profile mismatches, and coverage below 80% fail the job.
Unused inline definitions with zero CFG hashes remain uncovered. The threshold
has not been reduced to accommodate the measured deficit.

CI runs crypto ON/OFF on Ubuntu/macOS; full sanitizers on Ubuntu; mandatory local
MinIO; and the complete coverage gate. Live GCS is a separate explicit dispatch
job and missing credentials fail with an unavailable record. No skipped cloud
run, empty XML or empty mutation run is counted as success. Detailed local logs
are retained in the adjacent file-trans-system-audit-issues-20261013 workspace.

Issue #189 implements the regression infrastructure, but its coverage and
sanitizer acceptance gates remain open. See [mutation evidence](mutation-results-20261013.json).
