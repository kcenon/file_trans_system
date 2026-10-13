# Implementation and validation status

This local implementation series follows audit issues #175–#191. The upstream
repository remains archived (September 2026); no maintenance commitment or
publication is implied. Original issue closure is not acceptance evidence.

The PRD's seven phases are the shared roadmap for both READMEs. **Planned** means
requirements only; **Component** means isolated code/tests; **Integrated** means
the public API executes the feature; **Validated (scoped)** means the stated
workload passed. A validated workload does not certify every requirement in its
phase. No whole phase is marked complete.

<!-- roadmap:start -->
| PRD phase | Requirements | Current status and evidence | Remaining acceptance |
|---|---|---|---|
| 1. Core infrastructure | FR-08, FR-11, FR-14, FR-17/18 | Validated (scoped): codec, CRC/SHA, LZ4 and bounded pipeline [#175–#178, #188](protocol/PIPELINE_CAPACITY.md) | NFR-03/07/08 absolute performance unmeasured [#190](https://github.com/kcenon/file_trans_system/issues/190) |
| 2. Server foundation | FR-01/03/05/07/16 | Validated (scoped): real storage, requests, listing, quotas [#175–#179](validation/REGRESSION_GATES.md) | Broad platform/service qualification; retention policies have component evidence only |
| 3. Client foundation | FR-02/03/05/07, NFR-13 | Validated (scoped): TCP transfers, failure handling and reconnect [#176–#179, #185](protocol/RECONNECTION.md) | Optional authentication and TLS 1.3 NFR-16/17 remain open; reconnect does not imply resume |
| 4. Compression and resume | FR-09/11/14/15 | Validated (scoped): plain/encrypted process crash recovery and compression [#183, #186](protocol/RESUME.md) | Subsecond recovery only measured on a small loopback fixture; large-prefix validation remains linear |
| 5. Advanced features | FR-04/06/10/12/13/16/18 | Validated (scoped): batch, progress, throttling, quotas and admission [#187–#188](protocol/BATCH_TRANSFERS.md) | “Without degradation” and 5% bandwidth tolerance need qualification; 100 downloads pass, only 31/100 uploads pass [#190](validation/PUBLIC_API_BENCHMARKS.md) |
| 6. Integration and polish | PRD §10, NFR-01–10/22 | Integrated: logger, compiled examples and CI; monitoring adapters have component evidence [#189–#191](validation/REGRESSION_GATES.md) | Coverage 63.85% <80%; macOS dependency TSAN reports; leak freedom, Windows and dedicated LAN unverified |
| 7. QUIC (optional) | PRD §9 phase 7, NFR-16 | Component: public transport selection and TCP adapter [#184](protocol/TRANSPORTS.md) | QUIC handshake/timeout cleanup blocked in pinned dependency; 0-RTT/migration not validated end to end |
<!-- roadmap:end -->

The generated README tables use the same statuses, requirements and evidence.

## Acceptance boundaries

- Upload/download/list are real TCP operations. Tests wait for terminal outcomes
  and verify destination bytes and SHA-256; obtaining a handle is not completion.
  See [regression gates](validation/REGRESSION_GATES.md), including mutation proof.
- AES-256-GCM chunks use configured shared secrets, PBKDF2 for passwords and HKDF
  for per-transfer keys. This is not TLS 1.3, identity authentication or protection
  of every control message. See [encryption](protocol/ENCRYPTION.md), issue #183.
- Resume requires explicit checkpoint directories and resumable options; retained
  state has no automatic expiration. See [resume](protocol/RESUME.md), issue #186.
- Real S3 HTTP, multipart, ranges and presigned requests passed 32 local MinIO
  tests (#180/#181). AWS itself was not tested. GCS signed URL cryptography
  passed independent vectors (#182); live GCS is unavailable. Other GCS/Azure
  data operations retain unvalidated component/fallback paths; their example compilation
  is not cloud service validation.
- [Benchmarks](validation/PUBLIC_API_BENCHMARKS.md) report loopback measurements
  with raw artifacts. Local component benchmarks do not prove LAN throughput.
  The original 500 MB/s requirement remains a target, not a certified capability.
- Linux/macOS workflow definitions are configured; local execution used macOS
  arm64. Windows, remote GitHub CI and leak freedom remain unverified. Coverage
  and sanitizer jobs intentionally fail when their acceptance criteria are unmet.
- [Quick Start sources](../examples/quick_start_client.cpp) compile with the
  public headers. Both READMEs are generated from these sources and this roadmap;
  CI checks synchronization, builds examples and executes a verified round trip.

## Scope of issues #175–#191

Issues #175–#183 and #185–#188 have implemented behavior with scoped local
validation. #184 retains a QUIC dependency blocker. #189 supplies real regression
and quality gates but does not yet meet coverage/TSAN acceptance. #190 supplies
reproducible measurements; dedicated LAN and stable dedicated-host regression
baselines remain unavailable. #191 corrects examples and records these gaps
without declaring the entire original plan complete.
