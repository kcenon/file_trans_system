# Connection recovery

`with_auto_reconnect(enabled, reconnect_policy)` applies after loss of an
established connection. An initial `connect()` failure returns to its caller;
it does not start an implicit retry loop. Defaults are five attempts, a one-second
initial delay, multiplier two, and a 30-second cap. Zero attempts disables retries.
Delays must be nonnegative, the cap must be at least the initial delay, and the
multiplier must be finite and at least one. There is no jitter setting.

Loss transitions to `reconnecting`. One owned scheduler waits before each attempt,
then performs a connection attempt with `connect_timeout`. A successful attempt
transitions to `connected`; exhaustion transitions to `disconnected`.
`client_statistics::total_retries` counts attempted reconnects. The endpoint and
connection generation prevent overlapping sequences and stale successful attempts.

`disconnect()` is idempotent and cancels both backoff and a pending TCP connect.
A new explicit connect during backoff replaces the endpoint and old generation.
Destruction cancels pending connection work, joins the scheduler and transfer
workers, and stops the transport. State callbacks are serialized on the client
scheduler, outside connection and network callback locks; they may call
`disconnect()`. As with other observer APIs, retain the client throughout a callback
and destroy it from its owning thread, not inside its own callback.

Socket recovery does not imply file recovery. Current non-resumable operations
fail their waiting handles on connection loss. Persistent transfer recovery is
specified separately by issue #186; new requests can use the recovered socket.

Validation: `ReconnectSequenceTest` supplies a virtual clock and scripted attempts
to verify exact 10/20/35/35/35 ms delays, caps, attempt count, forwarded connect
deadline, success and cancellation. `PublicReconnectTest` uses actual loopback
server stop/restart, disabled retries, exhaustion, explicit cancellation,
destruction, callback reentrancy and endpoint replacement. Test processes have
external time bounds in addition to per-operation deadlines.
