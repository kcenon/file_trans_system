# Issues #176–#190: additional implementation and validation

This continuation starts at `eb50fe2` on
`implement/issues-176-190-followup`, using the refreshed 2026-10-15 prompts.
The [preceding report](FOLLOWUP_176_190.md) retains the issue-by-issue acceptance
mapping and historical measurements. Work remains local to this checkout.

## Storage-policy regressions addressed under #189

Six new tests cover production behavior using actual local storage:

- Direct deletion now checks legal hold, compliance mode and minimum retention.
  Previously, `execute_action` bypassed these checks and removed protected bytes.
- Pending deletion rechecks retention at execution time. A hold added after
  evaluation prevents the queued deletion. Dry runs preserve the exact content.
- A delete rule executes even when its target tier equals the current tier.
  Repeated evaluation replaces the pending decision for that key, and successful
  direct execution removes its pending entry.
- Full and prefix evaluation follow every listing page. A 1,003-object prefix
  plus an outside object verifies page completeness and prefix selection.
  Missing or repeated continuation tokens return an error.
- Evaluation, action and error callbacks run after releasing the callback mutex.
  Callbacks can unregister themselves and query policy statistics. The original
  callback test exceeded its 12-second external deadline.
- Retention exclusions apply consistently during evaluation and execution;
  missing metadata cannot silently authorize deletion under minimum retention.

The pre-fix run recorded five failing behavior tests and the callback timeout.
All 32 policy tests pass after the repair. These are policy-component checks;
they do not qualify cloud retention, global storage enforcement or tier migration.

## Session-cache correctness

Six new tests exercise the memory and file stores used by session resumption:

- Refreshing an existing ticket at capacity preserves other servers' tickets.
- Persisted tickets retain exact binary ticket data, server name, ALPN, early-data
  allowance and expiration; removal and clearing survive reopening the store.
- Truncated files, unsupported versions, invalid counts, oversized length fields
  and unrepresentable timestamps fail explicitly. Lengths are checked before
  allocation, and a failed reload preserves the existing in-memory tickets.
- Expired and nearly expired entries are removed; cleanup and capacity eviction
  preserve live entries.
- `store` reports persistence failure. Stream write/close errors leave the store
  dirty for a later retry. Successful write and close are required before clearing
  the dirty flag.

Four initial regressions failed against the previous implementation. The new
tests pass after repair. This strengthens the session-cache component associated
with #184; it does not establish a QUIC handshake, 0-RTT transfer or migration.
The [pinned QUIC dependency blocker](../protocol/TRANSPORTS.md) remains unchanged.

## Real HTTP adapter coverage

Two loopback integration tests exercise the production cloud HTTP adapter:
GET, string/binary POST, string/binary PUT, DELETE and HEAD. They verify the exact
request body, encoded query, custom header, response bytes and status, including
a 403 response. Connection failures are errors for every verb; retries stop at
the configured attempt count, and a later reachable endpoint can succeed.

The listener binds an ephemeral loopback port, caps request bytes and joins its
I/O thread during teardown. The failure endpoint reserves a port without
listening. No provider credentials are used. These tests verify HTTP behavior;
live AWS, GCS and Azure qualification is not inferred from them.

Cloud adapter sources are compiled only with encryption enabled, so these tests
use the same build condition. The first crypto-OFF build exposed the missing
test condition; its linker failure is retained with the corrected build evidence.

## Executed validation and remaining acceptance

The final production and test revision is `d5fbdbe`. The three implementation/test
commits are `77a6b56` (policy), `405d92c` (session cache), and `d5fbdbe` (HTTP).
[Machine-readable results](continuation-results-20261015.json) include commands,
dependency revisions, instrumentation inventory, source hashes and artifact paths.

| Configuration | Unit | Integration | Restart scenarios | Result |
| --- | ---: | ---: | ---: | --- |
| Final restored Debug, encryption ON | 1,076 | 162 | 4 | Pass |
| Debug, encryption OFF | 843 | 146 | 2 | Pass |
| ASAN | 1,076 | 162 | 4 | Pass; leak detection disabled |
| UBSAN | 1,076 | 162 | 4 | Pass |
| TSAN | 1,076 | 162 | 4 | Assertions pass; sanitizer gate fails |
| Fresh coverage profiles | 1,076 | 162 | 4 | Assertions pass; coverage gate fails |

Every suite above has zero skipped cases and zero assertion failures. TSAN still
reports three warnings involving Asio `clear_last_error`, `get_last_error` and
`config_get` accessing Darwin errno. Integration exits **-6** and the test driver
returns **1**. The stacks match the previously reduced pattern documented in the
[standalone reproducer](TSAN_ERRNO_REPRODUCER.md); that reproducer was not rerun
in this continuation. No suppression or successful TSAN gate is claimed.

Real MinIO passes **34 tests, zero skipped**, in both Debug and the coverage
configuration. All **nine expected mutation failures** occur, and the client
source SHA-256 before and after restoration is identical. The restored Debug
suite then passes in full. [Mutation details](mutation-continuation-20261015.json).
Both compiled Quick Starts verify exact upload/download bytes, SHA-256 and
missing-source rejection. README snippets and roadmap synchronization pass.

Whole-production coverage is **15,057 / 20,479 lines = 73.52409785634065%**,
up from **14,697 / 20,419 = 71.98%**. The unchanged 80% gate returns **1**.
Seven fresh profiles include the complete unit/integration/restart and MinIO
runs. There are no missing production translation units or invalid CFG profile
records. [Complete coverage inventory](coverage-continuation-20261015.json).
At this denominator, at least 1,327 more executable lines need meaningful
coverage to reach the threshold. The largest remaining gaps are QUIC transport,
Azure/GCS, storage manager and S3 branches.

The issue-by-issue implementation mapping remains in the preceding report.
The full suites revalidate the existing TCP transfer, locking, listing, crypto,
reconnect, resume, batch and backpressure regressions for #176–#188. #182 retains
its live-GCS gap. #184 remains blocked at the pinned QUIC dependency; no new
enabled-QUIC transfer success is claimed. **#189 remains incomplete** because the
coverage and TSAN gates fail. #190 retains its earlier 44/44 loopback measurement
and its dedicated-LAN/controlled-baseline qualification gaps. Live AWS/GCS/Azure,
Windows, remote GitHub workflows and leak freedom remain unverified.

## Validation provenance

Evidence is retained adjacent to the checkout in
`file-trans-system-completion-20261015/`: original failures, complete suite XML,
build logs, restart artifacts, fresh coverage profiles, MinIO results and
mutation/restoration evidence. This pass rebuilds the separate Debug, crypto-OFF,
ASAN, UBSAN, TSAN and coverage configurations provisioned in the preceding pass.
It does not describe reused directories as newly created builds.

Sanitizer builds instrument the project and the pinned thread, logger and network
dependencies. ASAN uses `detect_leaks=0:halt_on_error=1`; UBSAN uses
`halt_on_error=1:print_stacktrace=1`; TSAN runs without suppressions and uses
`halt_on_error=0:exitcode=66` to retain all reports. Suites run serially because
older fixtures share temporary paths. Each complete unit/integration suite has
a 600-second external deadline; restart verification has its own bounded timeout.
Compile-command inspection verifies instrumentation of all 35 production source
units and all 92 project-owned units including tests, plus all 52 thread, 28
logger and 90 network dependency-owned units. System libraries are outside this
inventory. The exact dependency commits remain in `cmake/dependency-revisions.json`.

Coverage profiles from the preceding binary were moved aside before execution.
The coverage gate still uses the complete production archive and all test
binaries, no project-source exclusions, encryption/LZ4 ON, QUIC OFF and the
unchanged 80% minimum. Historical benchmark data remain in the preceding report;
the 44-run Release benchmark was not repeated for these policy/cache changes.

The checked-in CI helpers reproduce the same gates:

```sh
cmake --build build --parallel 4
python3 tools/run_ci_tests.py --build build --suite-timeout 600 --artifacts artifacts/continuation/debug
python3 tools/check_regression_mutations.py --build build --artifacts artifacts/continuation/mutations
python3 tools/run_minio_tests.py --test-binary build/bin/file_trans_cloud_integration_tests --artifacts artifacts/continuation/minio
python3 tools/check_quick_start.py --build build --artifacts artifacts/continuation/quick-start
python3 tools/sync_readme_snippets.py
```

Run mutation checks with other builds and suites stopped. For the separate
sanitizer and coverage configurations, use the commands and environment recorded
in the machine-readable results and [regression gate instructions](REGRESSION_GATES.md).
