# Transport Abstraction Layer

## Overview

The transport abstraction layer provides a unified interface for different transport protocols (TCP, QUIC). This enables seamless switching between transports while maintaining consistent API usage throughout the file transfer system.

## Architecture

```
┌─────────────────────────────────────────┐
│         Application Layer               │
│    (file_transfer_client/server)        │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│       transport_interface               │
│  (Abstract base class)                  │
├─────────────────────────────────────────┤
│  - connect() / disconnect()             │
│  - send() / receive()                   │
│  - send_async() / receive_async()       │
│  - on_event() / on_state_changed()      │
│  - get_statistics()                     │
└─────────────────┬───────────────────────┘
                  │
       ┌──────────┴──────────┐
       │                     │
┌──────▼──────┐      ┌───────▼──────┐
│tcp_transport│      │quic_transport│
│             │      │  (Future)    │
└─────────────┘      └──────────────┘
```

## Components

### transport_interface

The base class that defines the transport contract:

```cpp
#include "kcenon/file_transfer/transport/transport_interface.h"

// Abstract interface methods
class transport_interface {
public:
    // Type identification
    virtual auto type() const -> std::string_view = 0;

    // Connection management
    virtual auto connect(const endpoint& remote) -> result<connection_result> = 0;
    virtual auto connect_async(const endpoint& remote)
        -> std::future<result<connection_result>> = 0;
    virtual auto disconnect() -> result<void> = 0;
    virtual auto is_connected() const -> bool = 0;
    virtual auto state() const -> transport_state = 0;

    // Synchronous data transfer
    virtual auto send(std::span<const std::byte> data,
                      const send_options& options = {}) -> result<std::size_t> = 0;
    virtual auto receive(const receive_options& options = {})
        -> result<std::vector<std::byte>> = 0;

    // Asynchronous data transfer
    virtual auto send_async(std::span<const std::byte> data,
                           const send_options& options = {})
        -> std::future<result<std::size_t>> = 0;
    virtual auto receive_async(const receive_options& options = {})
        -> std::future<result<std::vector<std::byte>>> = 0;

    // Event handling
    virtual void on_event(std::function<void(const transport_event_data&)> callback) = 0;
    virtual void on_state_changed(std::function<void(transport_state)> callback) = 0;

    // Statistics
    virtual auto get_statistics() const -> transport_statistics = 0;
};
```

### tcp_transport

TCP implementation of the transport interface:

```cpp
#include "kcenon/file_transfer/transport/tcp_transport.h"

// Create with default configuration
auto transport = tcp_transport::create();

// Create with custom configuration
auto config = transport_config_builder::tcp()
    .with_connect_timeout(std::chrono::seconds{10})
    .with_tcp_nodelay(true)
    .with_keep_alive(true, std::chrono::seconds{30})
    .build_tcp();

auto custom_transport = tcp_transport::create(config);
```

### transport_config

Configuration types for different transports:

```cpp
#include "kcenon/file_transfer/transport/transport_config.h"

// TCP configuration
tcp_transport_config tcp_config;
tcp_config.connect_timeout = std::chrono::seconds{10};
tcp_config.tcp_nodelay = true;
tcp_config.reuse_address = true;
tcp_config.keep_alive = true;

// QUIC configuration (future)
quic_transport_config quic_config;
quic_config.enable_0rtt = true;
quic_config.max_idle_timeout = std::chrono::seconds{30};
```

## Usage Examples

### Basic Connection

```cpp
auto transport = tcp_transport::create();

// Connect to server
auto result = transport->connect(endpoint{"localhost", 8080});
if (!result.has_value()) {
    std::cerr << "Connection failed: " << result.error().message << "\n";
    return;
}

// Send data
std::vector<std::byte> data = /* ... */;
auto send_result = transport->send(data);
if (send_result.has_value()) {
    std::cout << "Sent " << send_result.value() << " bytes\n";
}

// Receive data
auto recv_result = transport->receive();
if (recv_result.has_value()) {
    auto& received = recv_result.value();
    std::cout << "Received " << received.size() << " bytes\n";
}

// Disconnect
transport->disconnect();
```

### Asynchronous Operations

```cpp
auto transport = tcp_transport::create();

// Async connect
auto connect_future = transport->connect_async(endpoint{"localhost", 8080});

// Do other work...

// Wait for connection
auto result = connect_future.get();
if (!result.has_value()) {
    std::cerr << "Connection failed\n";
    return;
}

// Async send
std::vector<std::byte> data = /* ... */;
auto send_future = transport->send_async(data);

// Async receive
auto recv_future = transport->receive_async();

// Wait for results
auto sent = send_future.get();
auto received = recv_future.get();
```

### Event Handling

```cpp
auto transport = tcp_transport::create();

// State change callback
transport->on_state_changed([](transport_state state) {
    std::cout << "State: " << to_string(state) << "\n";
});

// Event callback
transport->on_event([](const transport_event_data& event) {
    switch (event.event) {
        case transport_event::connected:
            std::cout << "Connected\n";
            break;
        case transport_event::disconnected:
            std::cout << "Disconnected\n";
            break;
        case transport_event::data_received:
            std::cout << "Received " << event.data.size() << " bytes\n";
            break;
        case transport_event::error:
            std::cerr << "Error: " << event.error_message << "\n";
            break;
    }
});
```

### Using Transport Factory

```cpp
tcp_transport_factory factory;

// Check supported types
auto types = factory.supported_types();  // ["tcp"]

// Create transport from base config
tcp_transport_config config;
auto transport = factory.create(config);
```

## Transport States

| State | Description |
|-------|-------------|
| `disconnected` | Not connected to any remote endpoint |
| `connecting` | Connection in progress |
| `connected` | Successfully connected and ready for data transfer |
| `disconnecting` | Disconnection in progress |
| `error` | Error state requiring reconnection |

## Configuration Options

### Common Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `connect_timeout` | milliseconds | 30000 | Connection timeout |
| `read_timeout` | milliseconds | 0 | Read timeout (0 = no timeout) |
| `write_timeout` | milliseconds | 0 | Write timeout (0 = no timeout) |
| `send_buffer_size` | size_t | 0 | Send buffer size (0 = system default) |
| `receive_buffer_size` | size_t | 0 | Receive buffer size (0 = system default) |
| `keep_alive` | bool | true | Enable keep-alive |
| `keep_alive_interval` | seconds | 60 | Keep-alive interval |
| `max_retry_attempts` | size_t | 3 | Maximum retry attempts |
| `retry_delay` | milliseconds | 1000 | Delay between retries |

### TCP-Specific Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `tcp_nodelay` | bool | true | Disable Nagle's algorithm |
| `reuse_address` | bool | true | Enable SO_REUSEADDR |
| `reuse_port` | bool | false | Enable SO_REUSEPORT |
| `linger_timeout` | int | -1 | Linger timeout (-1 = disabled) |
| `keep_alive_probes` | int | 9 | Keep-alive probe count |
| `keep_alive_probe_interval` | seconds | 75 | Probe interval |

### QUIC-Specific Options (Future)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_0rtt` | bool | true | Enable 0-RTT resumption |
| `max_idle_timeout` | seconds | 30 | Maximum idle timeout |
| `max_bidi_streams` | uint64_t | 100 | Max bidirectional streams |
| `max_uni_streams` | uint64_t | 100 | Max unidirectional streams |
| `initial_max_data` | uint64_t | 10MB | Initial max data |

## Statistics

```cpp
auto stats = transport->get_statistics();

std::cout << "Bytes sent: " << stats.bytes_sent << "\n";
std::cout << "Bytes received: " << stats.bytes_received << "\n";
std::cout << "Packets sent: " << stats.packets_sent << "\n";
std::cout << "Packets received: " << stats.packets_received << "\n";
std::cout << "Errors: " << stats.errors << "\n";
std::cout << "RTT: " << stats.rtt.count() << " ms\n";
```

## Error Handling

Transport operations return `result<T>` types for error handling:

```cpp
auto result = transport->connect(endpoint{"localhost", 8080});

if (!result.has_value()) {
    switch (result.error().code) {
        case error_code::connection_failed:
            // Handle connection failure
            break;
        case error_code::connection_timeout:
            // Handle timeout
            break;
        case error_code::already_initialized:
            // Already connected
            break;
        default:
            // Other errors
            break;
    }
}
```

## Thread Safety

- All transport methods are thread-safe
- Callbacks are invoked from the I/O thread
- Statistics access is protected by mutex
- State changes are atomic

## QUIC Transport

The QUIC transport provides advanced features over TCP:
- 0-RTT connection resumption for reduced latency
- Multiplexed streams without head-of-line blocking
- Connection migration support
- Built-in TLS 1.3 encryption

### Basic QUIC Usage

```cpp
#include "kcenon/file_transfer/transport/quic_transport.h"

// Create QUIC transport with configuration
auto config = transport_config_builder::quic()
    .with_0rtt(true)
    .with_max_idle_timeout(std::chrono::seconds{60})
    .with_tls_config("cert.pem", "key.pem", "ca.pem")
    .build_quic();

auto transport = quic_transport::create(config);

// Connect (session tickets are automatically used if available)
auto result = transport->connect(endpoint{"example.com", 443});
if (result.has_value()) {
    // Check if 0-RTT was used
    if (transport->used_0rtt()) {
        std::cout << "Connected with 0-RTT\n";
    }
}
```

### 0-RTT Session Resumption

0-RTT allows clients to send data immediately upon reconnection without
waiting for a full handshake. This significantly reduces latency for
repeat connections.

#### Session Resumption Manager

```cpp
#include "kcenon/file_transfer/transport/session_resumption.h"

// Create session manager with persistent storage
session_resumption_config config;
config.enable_0rtt = true;
config.store_config.storage_path = "/path/to/sessions.dat";
config.store_config.max_tickets = 1000;
config.store_config.default_lifetime = std::chrono::hours{24 * 7};

// Optional callbacks for monitoring
config.on_0rtt_accepted = [](const std::string& server) {
    std::cout << "0-RTT accepted: " << server << "\n";
};
config.on_0rtt_rejected = [](const std::string& server) {
    std::cout << "0-RTT rejected: " << server << "\n";
};

auto manager = session_resumption_manager::create(config);

// Attach to transport
transport->set_session_manager(manager);
```

#### Connect with Early Data (0-RTT)

```cpp
// Prepare early data to send during 0-RTT handshake
std::vector<std::byte> early_data = /* request data */;

// Connect with 0-RTT - data is sent immediately if possible
auto result = transport->connect_with_0rtt(
    endpoint{"example.com", 443},
    early_data);

if (result.has_value()) {
    if (transport->is_0rtt_accepted()) {
        // Early data was accepted by server
    } else {
        // 0-RTT was rejected, data will be resent automatically
    }
}
```

#### Session Ticket Storage

Session tickets can be stored in memory or persisted to disk:

```cpp
// In-memory storage (default)
auto memory_store = memory_session_store::create();

// File-based storage for persistence across restarts
session_store_config config;
config.storage_path = "sessions.dat";
config.max_tickets = 1000;
auto file_store = file_session_store::create(config);

// Store a ticket manually
session_ticket ticket;
ticket.server_id = "example.com:443";
ticket.ticket_data = received_ticket_data;
ticket.expires_at = std::chrono::system_clock::now() + std::chrono::hours{24};
ticket.max_early_data_size = 16384;
store->store(ticket);

// Retrieve a ticket
if (auto ticket = store->retrieve("example.com:443")) {
    // Use ticket for 0-RTT connection
}
```

### 0-RTT Security Considerations

0-RTT data has replay attack vulnerabilities:
- Only send idempotent requests as early data
- Server may reject 0-RTT at any time
- Rejected early data is automatically resent after full handshake
- Session tickets have limited lifetime

### QUIC-Specific Methods

```cpp
// Create streams for multiplexing
auto stream_id = transport->create_stream();
if (stream_id.has_value()) {
    transport->send_on_stream(*stream_id, data);
    transport->close_stream(*stream_id);
}

// Create unidirectional stream
auto uni_stream = transport->create_unidirectional_stream();

// Check handshake status
if (transport->is_handshake_complete()) {
    auto alpn = transport->alpn_protocol();  // e.g., "file-transfer/1"
}

// Check 0-RTT status
bool can_0rtt = transport->is_0rtt_available();
bool used = transport->used_0rtt();
bool accepted = transport->is_0rtt_accepted();
```

### QUIC Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_0rtt` | bool | true | Enable 0-RTT resumption |
| `session.storage_path` | string | "" | Path to session ticket file |
| `session.max_tickets` | size_t | 1000 | Max stored tickets |
| `session.default_lifetime` | seconds | 7 days | Default ticket lifetime |
| `max_idle_timeout` | seconds | 30 | Maximum idle timeout |
| `max_bidi_streams` | uint64_t | 100 | Max bidirectional streams |
| `max_uni_streams` | uint64_t | 100 | Max unidirectional streams |
| `initial_max_data` | uint64_t | 10MB | Initial max data |
| `initial_max_stream_data` | uint64_t | 1MB | Initial max stream data |
| `alpn` | string | "file-transfer/1" | ALPN protocol identifier |
| `cert_path` | string | - | TLS certificate path |
| `key_path` | string | - | TLS private key path |
| `ca_path` | string | - | CA certificate path |
| `skip_cert_verify` | bool | false | Skip cert verification (testing only) |

## Related Documents

- [API Reference](api-reference.md)
- [Pipeline Architecture](pipeline-architecture.md)
- [Error Codes](error-codes.md)
