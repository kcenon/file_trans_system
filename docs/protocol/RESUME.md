# Durable transfer recovery

Issue #186 connects the public upload/download APIs to durable checkpoints on both endpoints. Enable it explicitly; ordinary transfers keep their previous wire format and cleanup behavior.

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("storage")
    .with_checkpoint_directory("server-checkpoints").build();
auto client = file_transfer_client::builder()
    .with_checkpoint_directory("client-checkpoints").build();
// Start/connect the endpoints before transferring.
upload_options options;
options.resumable = true;
auto transfer = client.value().upload_file("source.bin", "object.bin", options);
// In a fresh process, recreate/connect the client with the same configuration:
for (const auto& checkpoint : client.value().resumable_transfers()) {
    if (checkpoint.upload) {
        upload_options resumed;
        resumed.resume_id = checkpoint.id;
        auto handle = client.value().upload_file(checkpoint.local_path,
                                                 checkpoint.filename, resumed);
    }
}
```

Downloads use `download_options::resumable` / `resume_id` in the same way. Keep the same endpoint host/port, absolute local path, remote filename, compression, encryption credentials and overwrite policy. Credentials are supplied again by the application, never stored in a checkpoint. Resume negotiates fresh encryption salts and keys.

Each confirmed chunk writes its partial file, flushes it to stable storage, then atomically replaces and flushes the JSON checkpoint before acknowledging progress. Checkpoints contain a bitmap, original file SHA-256, transfer identity and options. A record SHA-256 detects accidental corruption; it is not protection against an attacker who can rewrite the checkpoint directory. A rolling hash verifies the durable partial-file prefix before recovery. Use private application-owned directories on a local filesystem with working atomic replacement and flush semantics. POSIX file/directory flushes were exercised on macOS; the Windows flush implementation has not been executed here.

The current protocol confirms an ordered prefix. Upload recovery accepts the server's durable prefix (including an acknowledgement lost in transit); download recovery supplies the client's durable prefix. Only subsequent chunk indices are sent. Changed sources, corrupt/missing records or partial files, inconsistent identity/options, unsupported server recovery, and admission rejection produce explicit failures. An already committed upload can reconcile a lost completion acknowledgement by matching persisted size and SHA-256; a completed local download similarly verifies its destination before discarding its checkpoint.

A network interruption with automatic reconnect enabled keeps the same transfer handle and retries after reconnection. An explicit disconnect or process exit retains resumable state for a later explicit `resume_id`. Successful completion removes checkpoint state. Cancellation of a live resumable transfer removes its state and partial data on the connected endpoints. If cancellation cannot reach the server, its retained files remain. There is no automatic expiry or remote offline-abandon API: operators must remove abandoned server JSON/partial pairs while the server is stopped. Retained upload partials reserve their full size against the runtime storage quota, including after restart.

Records are limited to 1 MiB (at most 3,900,000 chunk bitmap entries). A checkpoint does not grow with file contents. Source hashing and partial-prefix validation take time proportional to the bytes checked; recovery under one second is not guaranteed for arbitrarily large files or slow storage.

Validation: `ctest --test-dir build -R ProcessRestartResume --output-on-failure` launches separate client/server processes, exits the client after two durable chunks, kills the server, and starts both again. A TCP observer asserts actual resumed chunk indices 2–7 and exact destination bytes/SHA-256 for upload/download with plaintext and AES-GCM. The measured 522,833-byte loopback cases resumed progress in 18–35 ms with records below 1 MiB. `PublicResumeTest.*` additionally checks source changes, corrupt/missing state, server rejection, network-only recovery, cancellation, and lost completion acknowledgement.
