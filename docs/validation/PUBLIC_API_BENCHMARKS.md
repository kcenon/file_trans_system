# Public API benchmark — issue #190

The [2026-10-15 full-profile rerun](#follow-up-measured-results-2026-10-15) passes
44/44 runs, including all 100 simultaneous uploads and downloads. Earlier failed
measurements remain below and in their original raw artifact.

The executable uses the public client/server APIs over real TCP with separate
server and client-driver processes. Local splitting/assembly and compression
microbenchmarks in the other benchmark directories remain **component
microbenchmarks**; they are not network throughput evidence.

## Reproduce

Install the platform build prerequisites described in the README, then:

~~~sh
python3 tools/bootstrap_dependencies.py --build-directory build-release --build-type Release --jobs 6
python3 tools/build_transfer_benchmarks.py --build build-release
python3 tools/run_transfer_benchmarks.py --build build-release --profile full \
  --warmups 1 --repeats 3 --artifacts artifacts/public-api
~~~

The smoke profile uses 1 MiB/16 MiB files and four clients; full uses
1 MiB, 64 MiB and **1 GB = 1,000,000,000 bytes**, plus 100 connected clients.
Both include four concurrent 4 MiB downloads limited to 1 MiB/s per client and
8 MiB compressible text in both directions. Dataset generation uses a
deterministic SHA-256 counter stream for binary data and a fixed repeated
ASCII record for text. Generation and hashing outside the public API are not
included in transfer timing. Every case has a fresh server/client process pair,
one warmup, and three measured repetitions. Failed warmups also fail the run.

## Metrics and scope

- The timer starts **before calling upload_file/download_file**, including any
  synchronous source hashing. It ends after the handle reports verified terminal
  success. Connection setup is outside that interval. Aggregate throughput uses
  earliest API entry to latest verified outcome across the concurrent clients.
- Useful payload bytes and sent/received FTS1 application-frame bytes are
  separate. Application counters exclude TCP/IP/link headers and dependency
  framing; they are not a packet capture or physical interface byte counter.
- The driver independently hashes the persisted server destination for uploads
  and client destination for downloads. Missing outputs, mismatched bytes/hashes,
  missing transfer results, timeouts and nonzero subprocess exits fail the run.
  There is no successful empty benchmark array.
- Latency distributions include per-request elapsed seconds and progress
  callback spacing. **Callback spacing is not NFR-03 end-to-end chunk processing
  latency**; that target remains unmeasured.
- RSS comes from OS current-resident-size and process high-water marks:
  task_info/getrusage on macOS, /proc/self/statm/getrusage on Linux. Server idle
  baseline, server peak, connected client-driver idle and client-driver peak are
  separate byte-valued fields. At N=1 the driver is one client; at N>1 it is the
  **aggregate of N client instances**, not per-client RSS.
- Admission reservation bytes from [#188](../protocol/PIPELINE_CAPACITY.md) are
  conservative pipeline buffer charges, not RSS. Slow downloads exercise real
  TCP chunk acknowledgement flow control. Neither metric certifies memory
  behavior for arbitrary callback copies or unbounded caller-owned input.
- The 256 MiB + 123-byte S3 streaming check from #181 measured 7,487,488 bytes
  peak RSS growth with exact SHA-256; that cloud-specific experiment is separate
  from the TCP process measurements here.

The raw JSON schema has manifest, source, runs, summaries, targets and passed
fields. Each run records its workload, warmup flag, every transfer's byte
counters/hash/error/timestamps, independent destination verification, process
RSS and exit codes. Summaries report sample counts, mean, sample standard
deviation, min/max and nearest-observation p50/p95/p99. Three repetitions are a small
development sample, not a statistically established performance distribution.

The manifest records OS, CPU, memory, compiler, CMake flags, dependency build
flags and exact pinned revisions plus patch digests. Source and executable
digests identify an uncommitted development build. Local filesystem/cache state
and shared workstation load can affect results.

## Target qualification

PRD NFR-01/02 and SRS PERF-001/002 require at least 500 MB/s on a LAN with a 1 GB
file. The available environment is a shared Apple M4 Max workstation, 14 logical
CPUs and 38,654,705,664 bytes RAM, using loopback and a local filesystem. It has
no measured physical link capacity. **Dedicated LAN qualification is
unmeasured**; loopback numbers do not establish either a LAN pass or a LAN fail.

NFR-04's numeric server budget is 100,000,000 + 1,000,000 × connections bytes;
NFR-05's client idle budget is 50,000,000 bytes. The report compares local RSS
with those budgets and labels the result as a local observation. SRS PERF-024
is checked with 100 connected clients whose transfers must all verify.
The stronger PRD phrase “without degradation” is not inferred from that pass.
Per-core CPU (NFR-06), isolated codec speed (NFR-07/08), typical-text ratio
(NFR-09), and 10K-file listing latency (NFR-10) remain separately unmeasured by
this harness.

For future qualification, use two otherwise idle dedicated hosts, record NIC
and switch link capacities (enough for 500 MB/s), storage, MTU, OS tuning and
compiler/library flags, stage the same deterministic input outside the timed
window, and run both directions with repeated measurements. The C++ executable
accepts an optional host/bind address for this purpose; the Python orchestrator
currently automates loopback only. Retain both hosts' reports and independent
destination digests. TLS/QUIC comparisons remain blocked by #184.

Only compare regressions against a successful baseline with an identical
environment/workload manifest:

~~~sh
python3 tools/run_transfer_benchmarks.py --build build-release --profile full \
  --artifacts artifacts/candidate --baseline artifacts/baseline/results.json \
  --maximum-regression-percent 10
~~~

Mismatched, failed or incomplete baselines are rejected. A matched development
manifest still does not guarantee a stable host; use a dedicated, controlled
environment for release regression decisions. No such baseline was available
for this implementation. CI runs verified loopback workloads and uploads raw
results, with read-only repository permissions; it does not publish empty
latency/memory/scalability arrays or assert absolute LAN targets.

## Historical measured results (2026-10-13)

The checked-in raw artifact and measured table below record the executed full
profile on 2026-10-13. All throughput units are decimal MB/s; RSS is bytes in
JSON and decimal MB in the table.

[Raw results and complete environment manifest](public-api-20261013.json).

| Direction | File bytes | Clients | Compression / limit | Mean ± sample SD MB/s | Server peak MB (max) | Driver peak MB (max) |
|---|---:|---:|---|---:|---:|---:|
| upload | 1,048,576 | 1 | none | 21.89 ± 6.95 | 10.08 | 9.73 |
| download | 1,048,576 | 1 | none | 60.87 ± 1.04 | 9.75 | 8.80 |
| upload | 67,108,864 | 1 | none | 20.14 ± 2.18 | 10.34 | 9.96 |
| download | 67,108,864 | 1 | none | 74.20 ± 0.43 | 10.98 | 8.96 |
| upload | 1,000,000,000 | 1 | none | 19.27 ± 0.31 | 13.75 | 14.16 |
| download | 1,000,000,000 | 1 | none | 74.39 ± 0.60 | 11.70 | 10.06 |
| download | 1,048,576 | 100 | none | 161.12 ± 0.48 | 13.16 | 26.54 |
| download | 4,194,304 | 4 | none / 1 MiB/s each | 5.47 ± 0.00 | 11.75 | 14.66 |
| upload | 8,388,608 | 1 | adaptive | 26.20 ± 1.60 | 9.52 | 8.72 |
| download | 8,388,608 | 1 | adaptive | 107.70 ± 1.10 | 9.47 | 8.32 |

**Full profile: FAIL**, with 44 executed runs (11 workloads × one warmup plus
three repetitions). Forty runs passed exact-byte/SHA-256 verification. The four
100-client upload runs each verified **31/100 files**, with 69 failures per run:
“Server receive budget exhausted.” No throughput summary is calculated for that
failed workload. The four 100-client download runs each verified **100/100**.

At that revision, the server runtime receive executor used an 8 MiB queue and rejected a frame when
that budget was full (src/server/server_runtime.h, post/connect). A 256 KiB
uncompressed frame plus headers across 100 simultaneous upload clients exceeds
that budget. The workload exposes rejection instead of waiting for capacity.
This was the measured scalability/backpressure gap under issues #188/#190;
the earlier 100-connection small-RPC test from #189 did not establish 100 simultaneous
upload success. The benchmark does not relax the queue budget or change file
sizes to manufacture a passing result.

The numeric RSS observations for successful workloads are available per case.
File-size growth from 1 MiB through 1 GB did not produce proportional server or
single-client RSS growth in these runs. This is scoped empirical streaming
evidence, not a proof of leak freedom or arbitrary-workload bounded memory.
The failed upload workload's RSS and individual errors remain in the raw runs.

Acceptance mapping: real API timing, payload/frame bytes, exact persisted hashes,
separate process RSS, size/concurrency/slow-consumer variation, warmups/variance,
build and dependency manifests are implemented and executed. The 1 GB workload
passed in both directions. The 100-client workload was executed and exposes
the upload failure above. Dedicated LAN targets and a controlled regression
baseline remain unmeasured. Full-profile failure is returned as exit code 1.

## Follow-up measured results (2026-10-15)

**Full profile: PASS**, exit code 0. All **44/44 runs** and **848 persisted
files** passed independent byte/SHA-256 verification. This repeats the same 11
workloads, one warmup and three measured repetitions. The harness and runner
SHA-256 are identical to the historical run; clients, datasets and file sizes
were not reduced. Project production revision:
`266441887f74086e675437fcf29bdb6a36d3cdf1`; pending changes at measurement start
were documentation only. Project and dependency builds use Release (`-O3
-DNDEBUG`) on the Apple M4 Max / macOS 27.2 ARM64 loopback environment described
in the [new complete raw artifact](public-api-20261015.json).

| Direction | File bytes | Clients | Compression / limit | Mean ± sample SD MB/s | Server peak MB (max) | Driver peak MB (max) |
|---|---:|---:|---|---:|---:|---:|
| upload | 1,048,576 | 1 | none | 17.91 ± 1.06 | 9.57 | 9.72 |
| download | 1,048,576 | 1 | none | 62.63 ± 2.46 | 9.70 | 8.80 |
| upload | 67,108,864 | 1 | none | 20.65 ± 2.10 | 9.80 | 9.95 |
| download | 67,108,864 | 1 | none | 74.29 ± 1.08 | 11.52 | 9.80 |
| upload | 1,000,000,000 | 1 | none | 19.45 ± 0.25 | 11.40 | 11.85 |
| download | 1,000,000,000 | 1 | none | 75.01 ± 0.34 | 11.71 | 10.08 |
| upload | 1,048,576 | 100 | none | 23.55 ± 1.61 | 67.96 | 93.31 |
| download | 1,048,576 | 100 | none | 161.08 ± 0.78 | 13.19 | 25.46 |
| download | 4,194,304 | 4 | none / 1 MiB/s each | 5.47 ± 0.00 | 11.55 | 13.57 |
| upload | 8,388,608 | 1 | adaptive | 27.72 ± 0.91 | 9.55 | 8.70 |
| download | 8,388,608 | 1 | adaptive | 107.87 ± 0.61 | 9.47 | 8.36 |

Every one of the four 100-client upload runs now verifies **100/100 files**,
compared with **31/100** and 69 receive-budget failures in each historical run.
The four 100-client download runs still verify **100/100**. Runtime receive
admission now waits for capacity within the original 8 MiB queue budget;
[reservation, decoder and shutdown bounds](../protocol/PIPELINE_CAPACITY.md)
explain the additional memory outside that queue.

At 100 uploads, maximum measured server RSS is 67.96 MB and the aggregate
100-client driver RSS is 93.31 MB. The queue budget is not an RSS ceiling, and
driver RSS at N>1 is not per-client RSS. All measured server cases fall within
the numeric NFR-04 budget; all single-client idle observations fall within
NFR-05. File-size growth through 1 GB remains streaming in these scoped runs.
These observations do not establish leak freedom or arbitrary-workload bounds.

The decimal 1 GB means are **19.45 ± 0.25 MB/s upload** and **75.01 ± 0.34 MB/s
download**. Dedicated LAN throughput, NFR-03 chunk-processing latency, per-core
CPU and a controlled successful regression baseline remain **unmeasured**.
The former failed baseline cannot qualify a regression comparison. PERF-024's
100-connected-client completion check passes locally; “without degradation” and
bandwidth-tolerance qualification are not inferred. Historical raw failures
remain in `public-api-20261013.json`.
