# Socket.IO C++ Client

[![Build Status](https://github.com/socketio/socket.io-client-cpp/workflows/CI/badge.svg)](https://github.com/socketio/socket.io-client-cpp/actions)

By virtue of being written in C++, this client works in several different platforms. The [examples](https://github.com/socketio/socket.io-client-cpp/tree/master/examples) folder contains an iPhone, QT and Console example chat client! It depends on [websocket++](https://github.com/zaphoyd/websocketpp) and is inspired by [socket.io-clientpp](https://github.com/ebshimizu/socket.io-clientpp).

[![Clients with iPhone, QT, Console and web](https://cldup.com/ukvVVZmvYV.png)](https://github.com/socketio/socket.io-client-cpp/tree/master/examples)

## Compatibility table

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

## Features

- 100% written in modern C++11
- Binary support
- Automatic JSON encoding
- Multiplex support
- Similar API to the Socket.IO JS client
- Cross platform
- **Thread-safe listener management** - Add/remove listeners concurrently without crashes
- **Smart pointer memory management** - No memory leaks or dangling pointers
- **Build-type protocol selection** - HTTP for development, HTTPS for production
- **Modern dependencies** - ASIO 1.18.2, RapidJSON 1.1.0, WebSocketPP 0.8.2

Note: Only the WebSocket transport is currently implemented (no fallback to HTTP long-polling)

## Build Types

This library supports two build configurations:

### Debug Build (Development)
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```
- **Protocol**: HTTP/WebSocket only (`ws://`)
- **No TLS/SSL**: Smaller binary, faster compilation
- **Use case**: Local development with `http://localhost:3000`
- **Size**: ~24MB (with debug symbols)

### Release Build (Production)
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```
- **Protocol**: HTTPS/WebSocket Secure only (`wss://`)
- **TLS/SSL**: Secure connections with OpenSSL
- **Use case**: Production with `https://api.example.com`
- **Size**: ~2.7MB (optimized)

**Same library filename** (`libsioclient.a`), different capabilities based on how it's built.

## Installation

### Quick Start with CMake

```bash
# Clone with submodules (required for dependencies)
git clone --recurse-submodules https://github.com/yourusername/socket.io-client-cpp.git
cd socket.io-client-cpp

# Build for development (HTTP only)
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
make install

# OR build for production (HTTPS only)
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make install
```

### Dependencies

The library uses **git submodules** for dependencies:
- **ASIO** 1.18.2 - Asynchronous I/O
- **RapidJSON** 1.1.0 - JSON parsing
- **WebSocketPP** 0.8.2 - WebSocket protocol
- **OpenSSL** (system) - TLS/SSL (Release builds only)

To update submodules:
```bash
git submodule update --init --recursive
```

### Installation Alternatives

* [With CMAKE](./INSTALL.md#with-cmake)
* [Without CMAKE](./INSTALL.md#without-cmake)
* [With VCPKG](./INSTALL.md#with-vcpkg)
* [With Conan](./INSTALL.md#with-conan)
* [iOS and OS X](./INSTALL_IOS.md)
 * Option 1: Cocoapods
 * Option 2: Create a static library
 * Option 3: Manual integration

## Quickstart

**[Full overview of API can be seen here](./API.md)**

The APIs are similar to the JS client.

### Connect to a server
```C++
sio::client h;

// Debug build - use HTTP
h.connect("http://127.0.0.1:3000");

// Release build - use HTTPS
h.connect("https://api.example.com:3000");
```

### Emit an event

```C++
// emit event name only:
h.socket()->emit("login");

// emit text
h.socket()->emit("add user", username);

// emit binary
char buf[100];
h.socket()->emit("add user", std::make_shared<std::string>(buf,100));

// emit message object with lambda ack handler
h.socket()->emit("add user", string_message::create(username), [&](message::list const& msg) {
    // Handle acknowledgment
});

// emit multiple arguments
message::list li("sports");
li.push(string_message::create("economics"));
socket->emit("categories", li);
```
Items in `message::list` will be expanded in server side event callback function as function arguments.

### Bind an event

#### Bind with function pointer
```C++
void OnMessage(sio::event &)
{
    // Handle message
}
h.socket()->on("new message", &OnMessage);
```

#### Bind with lambda
```C++
h.socket()->on("login", [&](sio::event& ev)
{
    // Handle login message
    // Post to UI thread if any UI updating
});
```

#### Bind with lambda (simplified with auto-ack)
```C++
// New in v3.1.0: on_with_ack provides ack_message directly
h.socket()->on_with_ack("request", [](message::ptr const& msg, message::list& ack_msg) {
    // Process request
    std::cout << "Request: " << msg->get_string() << std::endl;

    // Send acknowledgment
    ack_msg.push(string_message::create("Success"));
});
```

#### Bind with member function
```C++
class MessageHandler
{
public:
    void OnMessage(sio::event &);
};
MessageHandler mh;
h.socket()->on("new message",std::bind( &MessageHandler::OnMessage,&mh,std::placeholders::_1));
```

#### Catch-all listener
```C++
// New in v3.1.0: Listen to all events
h.socket()->on_any([](sio::event& ev) {
    std::cout << "Event: " << ev.get_name() << std::endl;
});
```

### Using namespace
```C++
h.socket("/chat")->emit("add user", username);
```

### Connection state monitoring
```C++
// New in v3.1.0: Monitor connection state
h.set_state_listener([](sio::client::connection_state state) {
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
        case sio::client::connection_state::closing:
            std::cout << "Closing..." << std::endl;
            break;
    }
});
```

### Reconnection configuration
```C++
// New in v3.1.0: Configure reconnection behavior
sio::reconnect_config config(
    10,      // max attempts
    2000,    // initial delay (2s)
    10000    // max delay (10s)
);
h.set_reconnect_config(config);

// Or disable reconnection
h.set_reconnect_config(sio::reconnect_config::disabled());
```

### Thread Safety

All listener operations are thread-safe:
```C++
// Safe to call from multiple threads
std::thread t1([&]() { socket->on("event1", handler1); });
std::thread t2([&]() { socket->on("event2", handler2); });
std::thread t3([&]() { socket->off("event1"); });
```

**[Full overview of API can be seen here](./API.md)**

## Recent Improvements (v3.1.0)

### Memory & Thread Safety
- ✅ Fixed memory leaks in event listeners
- ✅ Thread-safe listener add/remove operations
- ✅ Smart pointer management for socket objects
- ✅ Atomic state management for reconnection logic

### Dependency Updates
- ✅ ASIO upgraded: 1.10.2 → 1.18.2
- ✅ RapidJSON upgraded: v1.0-beta → v1.1.0
- ✅ WebSocketPP updated to 0.8.2

### Build & Configuration
- ✅ Build-type-based protocol selection (Debug=HTTP, Release=HTTPS)
- ✅ Single library file for both configurations
- ✅ Zero compilation warnings
- ✅ Comprehensive unit tests for thread safety

### API Improvements
- ✅ **Reconnection configuration** - `set_reconnect_config()` to configure all reconnection parameters at once
- ✅ **Connection state observer** - Monitor connection state changes with `set_state_listener()`
- ✅ **Simplified event handlers** - `on_with_ack()` provides direct access to `ack_message` for easy custom responses
- ✅ **Wildcard event listener** - `on_any()` to listen to all events on a socket
- See [API.md](./API.md) for complete API documentation and examples

## License

MIT
