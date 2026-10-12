# Public transport selection

Both public builders accept `with_transport(transfer_transport_config)`, a value
variant of `tcp_transport_config` and `quic_transport_config`. Existing callers
use TCP. Upload, download, listing, and lifecycle events share one FTS1 codec and
server runtime. `transport_name()` reports the instantiated adapter identity;
client `network_bytes()` reports bytes sent/received by that adapter, including
FTS1 frames and encrypted/compressed application payloads (not IP/TCP headers).

TCP connects only after the actual connected event and honors the configured
connection deadline. Event-delivery mode avoids retaining a duplicate receive
queue. The server listener wraps accepted TCP sessions behind `accepted_transport`.
The pinned listener API binds by port; the endpoint host does not constrain its
bind address. TCP socket-tuning options are retained by the component config but
are not forwarded by the pinned messaging API.

QUIC public selection is disabled by default and is rejected explicitly, with no
TCP fallback. `-DFILE_TRANS_ENABLE_QUIC=ON` enables the experimental adapter and
its mandatory parameterized correctness test. It is **not qualified for use**.
The server adapter requires PEM certificate/key paths and forwards ALPN, CA,
stream counts, stream/data limits and idle timeout to the dependency. The client
forwards its QUIC configuration to the existing component. No file operation is
sent as 0-RTT data.

The enabled experiment on macOS ARM with the pinned network revision
`52d4947edfea87cbb9d6c51869ab8283a31ace25` failed on 2026-10-13: handshake exceeded
1.5 seconds; dependency timeout cleanup then crashed (exit 139) in
`quic_socket::~quic_socket`, after `messaging_quic_client::do_stop_impl` destroyed
its I/O context. Inspection also found the client dependency stores TLS options
without applying CA/verification configuration to its socket; the server creates
a different UDP socket/port for each accepted connection. These are dependency
prerequisites, not evidence of working QUIC file transfer. Issue #184 remains open
for QUIC qualification and dependency repair. Do not treat standalone QUIC unit
checks or skipped dependency handshake tests as end-to-end validation.

Reproduce (the enabled experiment may crash):

```sh
cmake -S . -B build -DFILE_TRANS_ENABLE_QUIC=ON
cmake --build build
build/bin/file_trans_integration_tests --gtest_filter='*PublicTransport*'
```

Default TCP validation uses the identical upload/list/download hash test,
unsupported-QUIC selection tests, failed-connect deadline tests, and existing
public protocol/encryption regressions. Certificates under `tests/test_data/quic`
are generated public fixtures, not operational credentials.
