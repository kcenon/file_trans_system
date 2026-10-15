# Issues #176–#190: 2026-10-15 implementation follow-up

Local branch: `implement/issues-176-190-followup`. Starting revision:
`bec276c095e1138c6eb082f49d8bab6ec60e393b`. This pass follows the refreshed
2026-10-15 prompts, preserves the earlier implementation, and does not modify
remote issues, publish changes, or alter the archived repository setting.
The [2026-10-13 report](ISSUES_175_191.md) remains historical evidence.
[Machine-readable per-issue results](followup-results-20261015.json) retain exact
counts, commands, failures, restoration hashes and local artifact references.

A [subsequent continuation](CONTINUATION_176_190.md) adds storage-policy and
session-cache repairs plus real HTTP adapter coverage. It reruns the full
validation matrix and measures 73.52% coverage. The results below retain this
earlier pass's provenance.

## Issue-by-issue results

| Issue | Work and acceptance evidence from this pass | Remaining boundary |
| --- | --- | --- |
| #176 | Existing ownership/locking behavior verified: eight focused integration cases each in Debug, TSAN and UBSAN | Final full sanitizer results below |
| #177 | Five focused public upload and deadline regressions pass; terminal outcomes and persisted contents checked | Public TCP scope |
| #178 | Eight public download/control/invalid-input regressions pass | Public TCP scope |
| #179 | Four public listing/failure regressions pass; hybrid continuation repaired under #189 | Hybrid listing consistency described below |
| #180 | 52 S3 unit cases pass; real signed requests exercised against MinIO | AWS service not executed |
| #181 | 11 streaming/failure unit cases pass; 256 MiB + 123 byte MinIO transfer verifies SHA-256 with 9,584,640 bytes peak RSS growth in the first rerun | Scoped S3 streaming observation |
| #182 | 12 signing/credential cases pass; independent signing vectors regenerated without changes | Live GCS unavailable: service account, bucket and object configuration absent |
| #183 | Moved policy coverage into an always-built test; crypto-OFF public client/server reject otherwise valid required key/password configurations. Six ON and two OFF policy cases, plus pipeline checks, pass | Shared-secret chunk authentication does not supply TLS or peer identity |
| #184 | Reproduced enabled-QUIC cleanup crash, repaired socket cancellation/draining and duplicate receive initiation in a pinned dependency patch. Four enabled cases now finish normally: three pass, real transfer fails handshake | TLS secrets, certificate verification and UDP routing remain dependency blockers; [details](../protocol/TRANSPORTS.md) |
| #185 | Ten reconnect/virtual-clock cases pass; full ASAN exposed and corrected a fixture ordering error for cumulative retries during server shutdown | Successful reconnect begins a new attempt sequence; transfer resume remains explicit |
| #186 | 31 checkpoint unit and eight public resume cases pass; four real plain/encrypted upload/download process restart scenarios pass | Opt-in checkpoints; no automatic expiration; prefix validation remains linear |
| #187 | 33 batch component and nine public batch cases pass, with every requested file accounted for | Public TCP scope |
| #188 | Runtime receive admission waits within the existing 8 MiB budget; six admission/decoder/public stress cases and 67 pipeline cases pass. New 100-client upload regression improved from 32/100 to 100/100; six focused fully instrumented TSAN cases pass | Pipeline and runtime reservations are separate; [memory and shutdown bounds](../protocol/PIPELINE_CAPACITY.md) |
| #189 | Added meaningful adapter lifetime, statistics concurrency, monitor/quota, hybrid pagination and cloud failure/persistence tests; repaired defects they exposed | Full coverage/sanitizer acceptance results below |
| #190 | Unchanged full Release profile passes 44/44 runs; all four 100-client uploads improve from 31/100 to 100/100 | Dedicated two-host LAN and controlled regression baseline unavailable |

## Additional defects exposed by #189

Adapter jobs now own their counter/tracker state after the adapter is destroyed.
The two lifetime regressions previously failed with an invalid mutex access.
The network pool reports configured workers instead of the instantaneous busy
worker count. Statistics sample/start timing is synchronized across producers
and observers. The latter fix addresses a source-level race; a separate TSAN
reproduction was not claimed.

Full TSAN then exposed concurrent console formatting by two logger instances.
Their per-writer mutexes did not protect the shared `std::cerr` formatting state.
A logger dependency patch formats each record locally, writes through the C
stream lock, and flushes through `fflush`; the integration test now runs two
independent writers concurrently. This concrete race is separate from the
standalone platform errno report. Bootstrap applies the logger patch alongside
the pinned thread/network patches.

Public monitoring and quota queries now observe committed runtime storage usage.
The former standalone quota scan stayed stale after public uploads. Advisory
`check_upload_allowed` uses live available capacity; actual uploads still reserve
capacity atomically in the runtime.

Hybrid listing independently exhausts each backend's opaque page tokens, merges
keys in sorted order with primary metadata taking precedence, and returns a
usable `fts-hybrid:` cursor. It rejects invalid cursors and non-advancing backend
tokens. Each request enumerates the union using O(N) metadata memory. The cursor
is a key position, not a snapshot: changes between requests can change the result.
This repairs correctness but does not qualify 10,000-file listing latency.

Cloud backend progress callbacks now receive a completed-operation notification
for successful stores and retrievals, outside the callback lock. This does not
claim incremental per-chunk cloud progress. Real MinIO tests exercise both cloud
and hybrid public upload/list/download; deleting the hybrid local copy forces
an exact-byte cloud fallback. Azure existence queries now propagate 401/403/429/
500 and transport errors instead of reporting a missing object. Azure/GCS HTTP
mock tests remain component evidence, not live provider qualification.

## Validation matrix

Executed suites and quality gates:

| Configuration | Unit | Integration | Process restart | Gate |
| --- | ---: | ---: | --- | --- |
| Debug, encryption ON | 1,064 passed | 160 passed | Four scenarios passed | Pass |
| Debug, encryption OFF | 831 passed | 146 passed | Two plain scenarios passed | Pass |
| Fresh ASAN | 1,064 passed | 160 passed | Four scenarios passed | Pass; leak detection disabled |
| Fresh UBSAN | 1,064 passed | 160 passed | Four scenarios passed | Pass; halt on undefined behavior |
| Fresh TSAN after logger repair | 1,064 assertions passed | 160 assertions passed | Four scenarios passed | **Fail**: three errno warnings; integration exit -6, driver exit 1 |

All counts above have zero skips and zero assertion failures. ASAN/UBSAN full
runs preceded the final logger dependency patch; the two affected observability/
concurrent-logger cases subsequently passed in each final build, including TSAN
with halt-on-error. The final full TSAN rerun contains no logger warning. Its
remaining reports and independent reproducer are documented [here](TSAN_ERRNO_REPRODUCER.md).
Original failed runs are retained, including ASAN's reconnect-fixture assertion
and the first full TSAN run's four warnings.

Real MinIO: **34 passed, zero skipped**, including two new backend/public cloud
and hybrid cases. The same 1,064 unit, 160 integration, four restart and 34 MinIO
cases ran in the fresh coverage configuration. The result is **14,697 / 20,419
executable production lines = 71.98%**, an increase from 12,986 / 20,338 = 63.85%.
The unchanged 80% gate returns **1**. No production translation units or real
CFG profiles are missing. [Complete inventory and counts](coverage-20261015.json).
Largest uncovered areas remain QUIC transport, Azure/GCS, storage manager/policy
and S3 failure branches. Unvalidated simulation paths were not exercised merely
to increase the percentage.

The first coverage-report attempt hit a non-UTF-8 byte in LLVM's human-readable
diagnostics. The reporter now retains raw diagnostic/report bytes and escapes
non-UTF-8 bytes for display; machine-readable JSON parsing and CFG mismatch checks
remain strict. The corrected run uses the same fresh profiles and still fails
the required coverage threshold.

All **nine expected mutation failures** occurred: no-op uploads, empty listings
and missing download output each failed three targeted tests. The client's
before/after source SHA-256 is identical, and the restored executable rebuilt
successfully. [Mutation results](mutation-results-20261015.json). Both encryption
ON/OFF compiled Quick Starts then passed exact bytes/SHA-256 and missing-source
rejection. All 20 ON and 14 OFF example targets compiled.

## Final #190 measurement

[All 44 raw runs](public-api-20261015.json) and [measurement tables](PUBLIC_API_BENCHMARKS.md)
are retained alongside the unchanged historical failures. Every one of 848
persisted outputs verifies. The four 100-client uploads now complete 100/100,
with zero receive-budget rejections, and all 100-client downloads pass. The full
profile returns 0 using the same harness, datasets, one warmup and three
measurements. Decimal 1 GB throughput is 19.45 ± 0.25 MB/s upload and
75.01 ± 0.34 MB/s download on loopback. At 100 uploads the measured server peak
is 67.96 MB and aggregate client-driver peak is 93.31 MB; the 8 MiB admission
budget is not a total RSS ceiling. Dedicated LAN qualification remains unmeasured.

## Acceptance mapping and remaining gates

- Upload, listing and download acceptance use real TCP, terminal waits, persisted
  bytes and independent SHA-256. Mutation checks prove that a no-op upload,
  empty listing and absent download output fail the integration assertions.
- Rejection, disconnect, corruption, cancellation, batch accounting, reconnect,
  authenticated encryption failure and crash recovery execute in the full
  integration suites. Crypto-OFF policy rejection is always compiled and tested.
- S3 HTTP, multipart/range streaming and public cloud/hybrid persistence execute
  against the pinned real MinIO service. Live AWS and GCS remain unavailable;
  Azure/GCS mocks and GCS signing vectors are component evidence only.
- The separate runtime receive queue now waits for capacity, bounds frame memory
  and admits lifecycle work during saturation. Deterministic stop/disconnect and
  reservation-release tests accompany the 100-client persistence regression.
- CI definitions retain full suites, crypto ON/OFF, sanitizers, MinIO, mutation
  checks and the strict coverage denominator. Local execution is recorded above;
  remote GitHub workflow results and Windows were not executed.
- #184 remains open for dependency QUIC handshake, key/certificate handling and
  UDP session routing. Timeout cleanup is repaired; a normal failed handshake
  is not a successful QUIC transfer.
- #189 remains open for **80% coverage** and a successful full unsuppressed TSAN
  gate on a suitable runtime. The errno reproducer narrows the remaining reports;
  it does not turn a failing run into a pass. Leak freedom remains unverified.
- #190 records loopback workload success/failure and process RSS separately from
  dedicated-LAN throughput, per-core CPU, chunk-processing latency and controlled
  regression qualification. Those unavailable measurements remain unmeasured.

## Reproduction and local artifacts

Use the pinned revisions in `cmake/dependency-revisions.json` and all checked-in
patches through `tools/bootstrap_dependencies.py`. No sibling checkout was reset.
Fresh ASAN/UBSAN/TSAN project builds use separately instrumented thread, logger
and network libraries. ASAN runs set `detect_leaks=0:halt_on_error=1`; they do not
establish leak freedom. UBSAN uses `halt_on_error=1:print_stacktrace=1`.

All 95 project, 52 thread, 41 logger and 90 network translation-unit compile
commands carry the selected sanitizer flag in each fresh sanitizer configuration.
System libraries are outside this instrumentation. The first full TSAN suite
completed its assertions in 298.8 seconds, close to the former 300-second process
deadline. `run_ci_tests.py --suite-timeout 600` supplies a bounded whole-suite
budget for sanitizer jobs and records it in results; per-test transfer/fault
budgets are unchanged.

```sh
# Repeat for address/ASAN, undefined/UBSAN, or thread/TSAN.
python3 tools/bootstrap_dependencies.py --build-directory build-tsan-review --sanitizer thread --jobs 4
python3 tools/ci_build.py --build build-tsan-review --sanitizer TSAN --dependency-build build-tsan-review
python3 tools/run_ci_tests.py --build build-tsan-review --suite-timeout 600 --artifacts artifacts/tsan
```

Coverage uses a fresh `profiles/` directory, encryption/LZ4 ON, and QUIC OFF,
merging the full unit/integration/restart suites plus real MinIO. The denominator
includes the complete production archive and all test binaries with no project
path exclusions. The unchanged required minimum is 80%. Disabled QUIC branches
and live provider environments are not represented as executed coverage.

The evidence directory adjacent to this checkout is
`file-trans-system-followup-20261015/`. It contains `status.json`, ordered focused
checks, original failures and corrected runs, build logs, full XML/logs, standalone
errno reproducers, raw profiles, MinIO logs and full benchmark artifacts. Empty
filters are retained as failed evidence selection, never counted as executed
validation. Test suites run serially because older fixtures share temp paths.

Commands are maintained in [regression gates](REGRESSION_GATES.md),
[benchmarks](PUBLIC_API_BENCHMARKS.md), and the refreshed prompt pack adjacent to
this checkout. The [errno reproducer](TSAN_ERRNO_REPRODUCER.md) has no project or
network-library dependency and uses no sanitizer suppression.
