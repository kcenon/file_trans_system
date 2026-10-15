# FTS1 encrypted transfers

Client builders accept `with_encryption_key()` (exactly 32 bytes) or
`with_encryption_password()` (nonempty). These are pre-shared secrets: configure
matching server builders. Setting one clears the other. Encryption is enabled by
these setters. Per-transfer `encrypt=false` explicitly requests plaintext;
`with_required_encryption()` on the server rejects it. Builds without
`FILE_TRANS_ENABLE_ENCRYPTION` reject encryption requests before processing.

The authenticated protocol extension is conditional on AES-256-GCM selection.
Existing plaintext framing is unchanged. Both peers must support this extension;
unsupported peers fail negotiation. Base `serialized_size` constants describe
fixed fields and exclude these extensions (and other variable fields).

1. A request includes a fresh random 32-byte client salt and a 32-byte HMAC-SHA256
   over its canonical FTS1 frame, with the tag zeroed. Passwords use the existing
   PBKDF2-SHA256 derivation (600,000 iterations); raw keys bypass PBKDF2.
2. HKDF-SHA256 expands that secret to separate 32-byte AES and HMAC keys. Its salt
   is the client salt; its info is `FTS1/AES256GCM/v1/{upload|download}/request/`
   followed by the 16 transfer-ID bytes. Secret material is never transmitted.
3. The server verifies the request, generates a fresh 32-byte server salt, and
   derives new keys with client || server salts and the `accepted/` domain.
   Acceptance includes the server salt and an HMAC using these accepted keys.
   The client requires the requested algorithm and verifies this proof.
4. Compress each original chunk first, then encrypt it. Chunk data is
   `12-byte IV || 16-byte GCM tag || ciphertext`; the encrypted flag and wire size
   include this overhead. GCM authenticates the transfer ID, index, offset,
   original and wire sizes, CRC32, and flags in big-endian field encoding.
   Each transfer owns its engine and fresh derived key. The engine combines
   random IV bytes with a counter; transfers reject counter exhaustion.
5. Decrypt before decompression and CRC verification. Chunk acknowledgements and
   completion/verified acknowledgements append an HMAC. This also authenticates
   empty transfers. These control messages omit their tag in the canonical HMAC
   input. Full-file SHA-256 and atomic commit remain mandatory.

Errors, cancellation, pause, connection management, and directory listing are not
covered by this transfer authentication extension. It is not a replacement for
TLS or a formally reviewed authenticated transport. Peers sharing one server
secret belong to the same trust domain; this is not per-user authorization.

Standalone `server_pipeline` users supply a thread-safe `encryption_provider`
that returns a keyed engine unique to the transfer ID. Keep its key immutable and
retain that engine throughout the transfer. Missing providers, keys, metadata,
and authentication failures stop processing without successful-stage callbacks.
The standalone pipeline authenticates ID/index/CRC/original size/compression as
AAD; its `encryption_metadata` contract is separate from FTS1 wire serialization.

Validation: `PublicEncryptionTest.*`, `EncryptionPolicyTest.*`,
`PipelineEncryptionPolicyTest.*`, and the existing AES/key-management tests.
The public tests cover exact hashes, visible ciphertext, wrong keys, tampering,
concurrent correct/incorrect clients, explicit plaintext, and required policy.
