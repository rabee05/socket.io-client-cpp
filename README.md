# Socket.IO C++ Client

[![Build Status](https://github.com/socketio/socket.io-client-cpp/workflows/CI/badge.svg)](https://github.com/socketio/socket.io-client-cpp/actions)

A modern, high-performance C++ client for Socket.IO with C++20 coroutine support, comprehensive error handling, and production-ready features.

[![Clients with iPhone, QT, Console and web](https://cldup.com/ukvVVZmvYV.png)](https://github.com/socketio/socket.io-client-cpp/tree/master/examples)

## Features

- **Modern C++20** - Coroutine support with async/await
- **Thread-Safe** - Concurrent listener management without crashes
- **Smart Pointers** - Automatic memory management, no leaks
- **Binary Support** - Send/receive binary data efficiently
- **Auto JSON Encoding** - Seamless JSON serialization
- **Namespace Support** - Multiple namespaces on single connection
- **Disconnect Tracking** - Detailed reason codes for troubleshooting
- **Flexible Reconnection** - Configurable exponential backoff
- **Cross-Platform** - iOS, Android, macOS, Linux, Windows

## Compatibility

<table>
  <tr>
    <th rowspan="2">C++ Client version</th>
    <th colspan="2">Socket.IO server version</th>
  </tr>
  <tr>
    <td align="center">1.x / 2.x</td>
    <td align="center">3.x / 4.x</td>
  </tr>
  <tr>
    <td>2.x (<code>2.x</code> branch)</td>
    <td align="center">YES</td>
    <td align="center">YES, with <code><a href="https://socket.io/docs/v4/server-initialization/#allowEIO3">allowEIO3: true</a></code></td>
  </tr>
  <tr>
    <td>3.x (<code>master</code> branch)</td>
    <td align="center">NO</td>
    <td align="center">YES</td>
  </tr>
</table>

## Quick Start

### Installation

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/yourusername/socket.io-client-cpp.git
cd socket.io-client-cpp

# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```

### Basic Usage

```cpp
#include "sio_client.h"

int main() {
    sio::client client;

    // Connect
    client.connect("http://localhost:3000");

    // Listen for events
    client.socket()->on("welcome", [](sio::event& ev) {
        std::cout << "Message: " << ev.get_message()->get_string() << std::endl;
    });

    // Emit events
    client.socket()->emit("hello", sio::string_message::create("world"));

    // Keep running
    std::this_thread::sleep_for(std::chrono::seconds(10));
    client.sync_close();

    return 0;
}
```

## Table of Contents

- [Installation](#installation-1)
- [API Overview](#api-overview)
- [C++20 Coroutines](#c20-coroutines)
- [Disconnect Handling](#disconnect-handling)
- [Advanced Features](#advanced-features)
- [Build Configuration](#build-configuration)
- [Examples](#examples)

## Installation

### Dependencies

Managed via git submodules:
- ASIO 1.18.2
- RapidJSON 1.1.0
- WebSocket++ 0.8.2
- OpenSSL (system, Release builds only)

### Build Options

#### Development (HTTP only)
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```
- Protocol: `ws://` (WebSocket)
- Size: ~24MB (with debug symbols)
- Use for: Local development

#### Production (HTTPS only)
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```
- Protocol: `wss://` (WebSocket Secure)
- Size: ~2.7MB (optimized)
- Use for: Production deployments

### Alternative Install Methods

- [CMake Integration](./INSTALL.md#with-cmake)
- [VCPKG](./INSTALL.md#with-vcpkg)
- [Conan](./INSTALL.md#with-conan)
- [iOS/macOS](./INSTALL_IOS.md)

## API Overview

### Connecting

```cpp
sio::client client;

// Simple connect
client.connect("http://localhost:3000");

// With authentication
auto auth = sio::object_message::create();
auth->get_map()["token"] = sio::string_message::create("secret");
client.connect("http://localhost:3000", auth);

// With query parameters
std::map<std::string, std::string> query{{"version", "1.0"}};
client.connect("http://localhost:3000", query);
```

### Event Handlers

```cpp
// Lambda
client.socket()->on("message", [](sio::event& ev) {
    auto msg = ev.get_message();
    std::cout << msg->get_string() << std::endl;
});

// Function pointer
void onConnect(sio::event& ev) { /* ... */ }
client.socket()->on("connect", &onConnect);

// Catch-all listener
client.socket()->on_any([](sio::event& ev) {
    std::cout << "Event: " << ev.get_name() << std::endl;
});
```

### Emitting Events

```cpp
// Simple emit
client.socket()->emit("ping");

// With data
client.socket()->emit("message", sio::string_message::create("Hello"));

// With acknowledgment callback
client.socket()->emit_with_ack("request", data, [](sio::message::list const& response) {
    std::cout << "Got response" << std::endl;
});

// With timeout
client.socket()->emit_with_ack("request", data,
    [](auto& response) { /* success */ },
    5000,  // timeout in ms
    []() { std::cerr << "Timeout!" << std::endl; }
);
```

### Auto-Acknowledgment

```cpp
// Simplified handler that provides ack_message directly
client.socket()->on_with_ack("request", [](sio::message::ptr const& msg, sio::message::list& ack_msg) {
    // Process request
    auto value = msg->get_int();

    // Send acknowledgment
    ack_msg.push(sio::string_message::create("Success"));
    ack_msg.push(sio::int_message::create(value * 2));
});
```

## C++20 Coroutines

Avoid callback hell with modern async/await syntax.

### Requirements

- C++20 compiler (GCC 10+, Clang 14+, MSVC 2019 16.8+)
- CMake 3.12+

### Basic Example

```cpp
#include "sio_client.h"

sio::emit_task async_operation(sio::socket::ptr socket) {
    // Sequential async operations - no nested callbacks!
    auto auth = co_await socket->emit_async("login", credentials);
    auto data = co_await socket->emit_async("getData", auth);
    auto result = co_await socket->emit_async("process", data);

    std::cout << "Done!" << std::endl;
    co_return result;
}

int main() {
    sio::client client;
    client.connect("http://localhost:3000");

    // Start the coroutine
    async_operation(client.socket());

    // Keep running
    std::this_thread::sleep_for(std::chrono::seconds(10));
    return 0;
}
```

### With Timeout

```cpp
sio::emit_task with_timeout(sio::socket::ptr socket) {
    try {
        // 5 second timeout
        auto response = co_await socket->emit_async("slowOp", params, 5000);
        std::cout << "Success!" << std::endl;
        co_return response;
    } catch (const sio::timeout_exception& e) {
        std::cerr << "Operation timed out" << std::endl;
        co_return sio::message::list();
    }
}
```

### Before vs. After

**Before (Callback Hell):**
```cpp
socket->emit_with_ack("step1", data1, [socket](auto& r1) {
    socket->emit_with_ack("step2", r1, [socket](auto& r2) {
        socket->emit_with_ack("step3", r2, [](auto& r3) {
            // Finally done!
        });
    });
});
```

**After (Clean Coroutines):**
```cpp
sio::emit_task workflow(sio::socket::ptr socket) {
    auto r1 = co_await socket->emit_async("step1", data1);
    auto r2 = co_await socket->emit_async("step2", r1);
    auto r3 = co_await socket->emit_async("step3", r2);
    co_return r3;
}
```

See [examples/Console/coroutine_example.cpp](examples/Console/coroutine_example.cpp) for a complete example.

## Disconnect Handling

Track exactly why connections are lost for better error handling and recovery.

### Disconnect Reasons

```cpp
enum class disconnect_reason {
    client_disconnect,      // User called close()
    server_disconnect,      // Server closed connection
    transport_close,        // WebSocket closed cleanly
    transport_error,        // Network/protocol error
    ping_timeout,          // Server not responding to pings
    namespace_disconnect,   // Namespace-specific disconnect
    max_reconnect_attempts  // Gave up reconnecting
};
```

### Usage

```cpp
client.set_close_listener([](sio::client::disconnect_reason reason) {
    switch(reason) {
        case sio::client::disconnect_reason::ping_timeout:
            std::cout << "Server stopped responding" << std::endl;
            // Maybe increase ping timeout or alert monitoring
            break;

        case sio::client::disconnect_reason::transport_error:
            std::cout << "Network error" << std::endl;
            // Check connectivity, maybe retry with different network
            break;

        case sio::client::disconnect_reason::max_reconnect_attempts:
            std::cout << "Could not reconnect after multiple attempts" << std::endl;
            // Show offline mode, notify user
            break;

        case sio::client::disconnect_reason::server_disconnect:
            std::cout << "Server closed connection (kicked or shutdown)" << std::endl;
            break;

        default:
            std::cout << "Disconnected" << std::endl;
            break;
    }
});
```

### Smart Reconnection

```cpp
class SmartClient {
    sio::client client_;
    std::atomic<int> ping_timeout_count_{0};

public:
    SmartClient() {
        client_.set_close_listener([this](sio::client::disconnect_reason reason) {
            if (reason == sio::client::disconnect_reason::ping_timeout) {
                ping_timeout_count_++;
                if (ping_timeout_count_ >= 3) {
                    std::cout << "Multiple ping timeouts - server may be down" << std::endl;
                    // Try different server or notify ops team
                }
            }
        });

        client_.set_open_listener([this]() {
            ping_timeout_count_ = 0;  // Reset on success
        });
    }
};
```

## Advanced Features

### Reconnection Configuration

```cpp
// Configure all parameters at once
sio::reconnect_config config(
    10,      // max attempts
    2000,    // initial delay (2s)
    10000    // max delay (10s)
);
client.set_reconnect_config(config);

// Or use presets
client.set_reconnect_config(sio::reconnect_config::disabled());
```

### Connection State Monitoring

```cpp
client.set_state_listener([](sio::client::connection_state state) {
    switch(state) {
        case sio::client::connection_state::connecting:
            std::cout << "Connecting..." << std::endl;
            break;
        case sio::client::connection_state::connected:
            std::cout << "Connected!" << std::endl;
            break;
        case sio::client::connection_state::reconnecting:
            std::cout << "Reconnecting..." << std::endl;
            break;
        case sio::client::connection_state::disconnected:
            std::cout << "Disconnected" << std::endl;
            break;
    }
});
```

### Namespaces

```cpp
// Default namespace
auto default_socket = client.socket();

// Custom namespace
auto chat = client.socket("/chat");
auto admin = client.socket("/admin");

// Use them independently
chat->emit("message", sio::string_message::create("Hello"));
admin->emit("command", sio::string_message::create("status"));
```

### Thread Safety

```cpp
// All operations are thread-safe
std::thread t1([&]() { socket->on("event1", handler1); });
std::thread t2([&]() { socket->on("event2", handler2); });
std::thread t3([&]() { socket->off("event1"); });

// All will execute safely without crashes
t1.join();
t2.join();
t3.join();
```

### Connection Metrics

```cpp
auto metrics = socket->get_metrics();
std::cout << "Packets sent: " << metrics.packets_sent << std::endl;
std::cout << "Packets received: " << metrics.packets_received << std::endl;
std::cout << "Ping latency: " << metrics.last_ping_latency.count() << "ms" << std::endl;
```

## Build Configuration

### Custom io_context

```cpp
// Share io_context with your application
asio::io_context io_context;

sio::client_options opts;
opts.io_context = &io_context;

sio::client client(opts);
client.connect("http://localhost:3000");

// Run in your event loop
io_context.run();
```

### Logging

```cpp
// Enable all logs
client.set_logs_verbose();

// Quiet mode (errors only)
client.set_logs_quiet();

// Default (connection events + errors)
client.set_logs_default();
```

## Examples

- [Console Chat](examples/Console/main.cpp) - Basic CLI chat client
- [C++20 Coroutines](examples/Console/coroutine_example.cpp) - Modern async/await
- [QT Chat](examples/QT/SioChatDemo/) - GUI application
- [iOS Chat](examples/iOS/SioChatDemo/) - Mobile application

## What's New in v3.2.0

### Performance
- ✅ Replaced `std::bind` with lambdas (2-5% faster event dispatch)
- ✅ Optimized string handling with move semantics
- ✅ Improved async I/O with modern ASIO patterns
- ✅ Atomic state management for lock-free operations

### Features
- ✅ C++20 coroutine support with `emit_async()`
- ✅ Detailed disconnect reason tracking
- ✅ Auto-acknowledgment with `on_with_ack()`
- ✅ Catch-all listener with `on_any()`
- ✅ Connection state observer
- ✅ Flexible reconnection configuration
- ✅ Connection metrics and monitoring

### Reliability
- ✅ Fixed memory leaks in event listeners
- ✅ Thread-safe listener operations
- ✅ Smart pointer management (no manual delete)
- ✅ Comprehensive unit tests for thread safety
- ✅ Zero compilation warnings

### Dependencies
- ✅ ASIO: 1.10.2 → 1.18.2
- ✅ RapidJSON: v1.0-beta → v1.1.0
- ✅ WebSocket++: Updated to 0.8.2

## Documentation

- [API Reference](./API.md) - Complete API documentation
- [Installation Guide](./INSTALL.md) - Detailed build instructions
- [iOS Setup](./INSTALL_IOS.md) - iOS-specific instructions
- [Compression Analysis](./COMPRESSION_ANALYSIS.md) - Performance considerations

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Support

- GitHub Issues: [Report bugs or request features](https://github.com/socketio/socket.io-client-cpp/issues)
- Stack Overflow: Tag with `socket.io` and `c++`
- Socket.IO Slack: [Join the community](https://socketio.slack.com)
