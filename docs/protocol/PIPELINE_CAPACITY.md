# Pipeline capacity and shutdown

Issue #188 replaces unused stage queues with admission limits on the actual shared thread-pool path. Every accepted chunk owns one reservation from its first job through its last callback. Stage handoffs move that reservation and never wait for another slot. Thus one worker can run every stage without waiting for work queued behind itself.

The invariants are: admitted chunks ≤ `queue_size`; global reserved bytes ≤ `max_buffered_bytes`; and each transfer's reserved bytes ≤ `max_memory_per_transfer`. Defaults are 64 chunks, 64 MiB globally and 32 MiB per transfer. A reservation charges four times the largest original/input buffer capacity, plus 4 KiB for job/metadata overhead; supplied crypto metadata capacities are included. This covers simultaneous input/transformed buffers and compression/encryption scratch. Downloads reserve before allocating their read buffer. Impossible or overflowing requests fail immediately.

These are conservative buffer reservations, not measured process RSS. Fixed worker/library/allocator overhead, caller-owned input, and copies retained by application callbacks are outside the budget. A blocked producer retains its argument until admission or failure; applications should bound their producer count. Copying a callback chunk creates an application-owned copy; its admission token stays with the pipeline's moved buffer.

The original submit methods wait for admission. Overloads accept a timeout and `std::stop_token`; zero timeout is nonblocking, and `try_submit_upload_chunk` uses it. Timeout/cancellation/shutdown return errors without leaking capacity. A stop token cancels the admission/bandwidth wait; it does not revoke an already accepted job. Blocking submission from a pipeline callback is rejected; use nonblocking submission or a separate producer. Joining the pipeline from its own callback is also rejected.

`stop(true)` first closes admission and wakes blocked producers, then drains accepted work before joining workers. `stop(false)` closes admission, cancels processing and clears pending jobs, then joins executing workers. Neither can preempt a user callback: callbacks must return for shutdown to finish. Error paths, callback exceptions, rejected enqueue and cleared jobs release reservations automatically. Configure callbacks/providers before starting work.

`queue_sizes()` counts real prepared/queued jobs by stage, excluding executing jobs. `admitted_chunks` and `reserved_bytes` include queued, executing and callback-held pipeline work. Their peak counters record maxima. `backpressure_events` counts admission attempts that encounter insufficient capacity once per attempt, including nonblocking rejection. Resetting statistics retains current capacity gauges.

Validation: the re-enabled `ServerPipelineTest.BackpressureWithSmallQueue` holds a consumer deterministically and checks exact queued/admitted counts, rejection, timeout and blocking release. Other tests cover cancellation while blocked, immediate/graceful saturated shutdown, per-transfer limits, impossible sizes, errors and throwing callbacks. A single worker processes 64 MiB through a 532,480-byte reservation budget and verifies each chunk CRC. The 68 pipeline tests passed in Debug and ThreadSanitizer; 38 public transfer regressions passed in Debug and 22 in ThreadSanitizer. Dependency libraries are not sanitizer-instrumented.

## Public receive admission (2026-10-15 follow-up)

The public server now waits for capacity instead of rejecting healthy bursts at
its existing 8 MiB receive budget. Its ordered control worker is independent of
the TCP I/O runner and the pipeline workers. A full queue delays the receive
callback, so the dependency does not issue its next socket read until capacity
returns. Capacity release does not need another network callback: sends enqueue
asynchronously, and download acknowledgements are separate control operations.
The dependency patch `network-system-receive-callback.patch` invokes receive
callbacks outside its callback-registry mutex so disconnect can wake admission.

The runtime admits at most 256 data frames and 8 MiB of reservations, including
executing work. Each frame reserves three times its vector capacity for the
encoded frame, decoded payload and decoded message/chunk, plus task/closure
metadata. The worker destroys its closure before releasing the reservation,
including on exceptions. Pipeline transformation reservations remain separate.
Lifecycle tasks bypass data capacity so cancellation cannot wait behind its own
release. Connection slots are reserved before allocating a decoder or queuing
admission; at most three lifecycle records per reserved connection can be pending.

Ingress buffers are also bounded, but are separate from that 8 MiB gauge. Each
of at most `max_connections` peers has one partial frame capped at 1,118,208 bytes
(1 MiB maximum chunk + 64 KiB expansion + 4 KiB protocol allowance). Oversized
advertised lengths fail before allocating the body. The streaming decoder
delivers one completed frame at a time; it does not collect a vector of unadmitted
frames. The pinned TCP listener has one I/O runner: at most one completed frame
can wait outside admission, with at most two frame-sized buffers during decoder
verification. The dependency also owns one 4 KiB read buffer per socket and a
4 KiB callback fragment. These bounds exclude allocator overhead, OS socket
buffers, outbound sends, application-owned copies and fixed library memory.
They are reservations/allocation bounds, not an RSS claim or QUIC qualification.

Admitted frames execute FIFO. Public uploads send one chunk per acknowledgement,
so a client cannot accumulate an unbounded sequence of admitted chunks. The TCP
executor decides which ready peer is read next; no strict per-peer scheduling
latency is promised. Disconnect cancels that peer's admission and wakes its
waiter. Stop closes admission, wakes all waiters, then drains accepted tasks
before joining the control worker. A running application callback must return
for stop to finish. Runtime stop has graceful semantics; the component pipeline
separately supports immediate cancellation as described above. Admission from
the control worker itself and impossible reservations fail without waiting.

New regressions cover executing-closure lifetime, release after a throwing task,
disconnect while saturated, stop wakeup before consumer completion, impossible
and reentrant submission, oversized stream headers, coalesced delivery, and 100
simultaneous public uploads of 1 MiB uncompressed files with independent hashes.
The last test failed before the receive-admission fix and now passes.
