## API
### *Overview*
There're just 3 roles in this library - `socket`, `client` and `message`.

`client` is for physical connection while `socket` is for "namespace" (which is like a logical channel), which means one `socket` paired with one namespace, and one `client` paired with one physical connection.

Since a physical connection can have multiple namespaces (which is called multiplex), a `client` object may have multiple `socket` objects, each of which is bound to a distinct `namespace`.

Use `client` to setup the connection to the server, manange the connection status, also session id for the connection.

Use `socket` to send messages under namespace and receives messages in the namespace, also handle special types of message.

The `message` is just about the content you want to send, with text, binary or structured combinations.

### *Socket*
#### Constructors
Sockets are all managed by `client`, no public constructors.

You can get it's pointer by `client.socket(namespace)`.

#### Event Emitter
`void emit(std::string const& name, message::list const& msglist = nullptr)`

Fire-and-forget event emission. Sends an event without expecting an acknowledgment from the server.

`void emit_with_ack(std::string const& name, message::list const& msglist, std::function<void(message::list const&)> const& ack)`

**New in v3.2.0**: Emit an event and receive an acknowledgment callback from the server. Use this when you need to know the server has processed your event and potentially receive response data.

`void emit_with_ack(std::string const& name, message::list const& msglist, std::function<void(message::list const&)> const& ack, unsigned timeout_ms, std::function<void()> const& timeout_callback)`

**New in v3.2.0**: Emit with acknowledgment and timeout support. If the server doesn't respond within the specified timeout, the timeout callback is invoked and the ack callback is cancelled.

`emit_task emit_async(std::string const& name, message::list const& msglist = nullptr)`

**C++20 Coroutine Support**: Modern async/await style emit with acknowledgment. Returns an awaitable task that suspends until the server responds. Use `co_await` to get the response.

`emit_task emit_async(std::string const& name, message::list const& msglist, unsigned timeout_ms)`

**C++20 Coroutine Support**: Async emit with timeout. Throws `sio::timeout_exception` if the server doesn't respond within the specified timeout.

```C++
// Fire-and-forget
socket->emit("chat", string_message::create("Hello!"));

// With acknowledgment (callback style)
socket->emit_with_ack("getData",
    message::list(int_message::create(42)),
    [](message::list const& response) {
        std::cout << "Server responded with: " << response.to_array_message()->get_vector().size() << " items" << std::endl;
    });

// With acknowledgment and timeout (callback style)
socket->emit_with_ack("getData",
    message::list(int_message::create(42)),
    [](message::list const& response) {
        std::cout << "Server responded!" << std::endl;
    },
    5000,  // 5 second timeout
    []() {
        std::cout << "Request timed out - server did not respond" << std::endl;
    });

// C++20 Coroutine style (modern async/await)
sio::emit_task handle_request() {
    // Simple async request
    auto response = co_await socket->emit_async("getData", message::list(int_message::create(42)));
    std::cout << "Server responded with: " << response.size() << " items" << std::endl;

    // With timeout and error handling
    try {
        auto result = co_await socket->emit_async("slowOperation", nullptr, 5000);
        std::cout << "Operation completed successfully" << std::endl;
    } catch (const sio::timeout_exception& e) {
        std::cout << "Operation timed out: " << e.what() << std::endl;
    }

    // Chain multiple async operations
    auto auth = co_await socket->emit_async("authenticate", credentials);
    auto userData = co_await socket->emit_async("getUserData", auth);
    auto profile = co_await socket->emit_async("getProfile", userData);

    co_return profile;
}
```

#### Event Bindings
`void on(std::string const& event_name,event_listener const& func)`

`void on(std::string const& event_name,event_listener_aux const& func)`

Bind a callback to specified event name. Same as `socket.on()` function in JS, `event_listener` is for full content event object, `event_listener_aux` is for convenience.

`void on_with_ack(std::string const& event_name, event_handler_with_ack const& handler)`

**New in v3.2.0**: Simplified event handler that provides direct access to acknowledgment message. No need to check `need_ack()` manually.

`void on_any(event_listener const& func)`

`void on_any(event_listener_aux const& func)`

**New in v3.2.0**: Bind a "catch-all" listener that receives all events on this socket. Useful for debugging or logging.

`void off(std::string const& event_name)`

Unbind the event callback with specified name.

`void off_all()`

Clear all event bindings (not including the error listener).

`void on_error(error_listener const& l)`

Bind the error handler for socket.io error messages.

`void off_error()`

Unbind the error handler.

```C++
//event object:
class event
{
public:
    const std::string& get_nsp() const;

    const std::string& get_name() const;

    const message::ptr& get_message() const;

    const message::list& get_messages() const;  // New in v3.1.0

    bool need_ack() const;

    void put_ack_message(message::list const& ack_message);

    message::list const& get_ack_message() const;
   ...
};
//event listener declare:
typedef std::function<void(const std::string& name,message::ptr const& message,bool need_ack, message::list& ack_message)> event_listener_aux;

typedef std::function<void(event& event)> event_listener;

typedef std::function<void(message::ptr const& message, message::list& ack_message)> event_handler_with_ack;  // New in v3.1.0

typedef std::function<void(message::ptr const& message)> error_listener;

```

#### Connect and close socket
`connect` will happen for existing `socket`s automatically when `client` have opened up the physical connection.

`socket` opened with connected `client` will connect to its namespace immediately.

`void close()`

Positively disconnect from namespace.

#### Get name of namespace
`std::string const& get_namespace() const`

Get current namespace name which the client is inside.

#### Get connection metrics
`connection_metrics get_metrics() const`

**New in v3.2.0**: Retrieves connection statistics and health metrics for this socket.

Returns a `connection_metrics` struct containing:
- `packets_sent`: Number of packets sent through this socket
- `packets_received`: Number of packets received by this socket
- `reconnection_count`: Total number of reconnection attempts made by the client
- `last_ping_latency`: Most recent ping round-trip time in milliseconds
- `connected_at`: Timestamp when the socket was connected

**Example:**
```cpp
auto metrics = socket->get_metrics();
std::cout << "Packets sent: " << metrics.packets_sent << std::endl;
std::cout << "Packets received: " << metrics.packets_received << std::endl;
std::cout << "Reconnections: " << metrics.reconnection_count << std::endl;
std::cout << "Ping latency: " << metrics.last_ping_latency.count() << "ms" << std::endl;

// Calculate uptime
auto now = std::chrono::system_clock::now();
auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - metrics.connected_at);
std::cout << "Uptime: " << uptime.count() << " seconds" << std::endl;
```

### *Client*
#### Constructors
`client()` default constructor.

#### Connection Listeners
`void set_open_listener(con_listener const& l)`

Call when websocket is open, especially means good connectivity.

`void set_fail_listener(fail_listener const& l)`

**New in v3.2.0**: Called when connection fails with detailed error reason.

`void set_close_listener(close_listener const& l)`

**New in v3.2.0**: Called when connection closes with detailed disconnect reason.

`void set_state_listener(state_listener const& l)`

**New in v3.2.0**: Monitor all connection state changes (disconnected, connecting, connected, reconnecting, closing).

`void clear_con_listeners()`

**New in v3.2.0**: Clear all connection-related listeners (open, fail, close, reconnect, reconnecting).

```C++
//connection listener enums and typedefs:

// Detailed disconnect reasons
enum class disconnect_reason
{
    client_disconnect,      // User called close()
    server_disconnect,      // Server sent disconnect packet
    transport_close,        // WebSocket closed cleanly
    transport_error,        // WebSocket error
    ping_timeout,          // Server stopped responding to pings
    namespace_disconnect,   // Namespace-specific disconnect
    max_reconnect_attempts  // Reconnection attempts exhausted
};

// Connection failure reasons
enum class connection_error
{
    timeout,                  // Connection timeout
    network_failure,          // Network unreachable, DNS failure
    protocol_error,           // Invalid socket.io protocol
    authentication_failed,    // Auth rejected by server
    transport_open_failed,   // WebSocket handshake failed
    ssl_error,                // TLS/SSL error (Release builds)
    unknown                   // Unknown error
};

enum class connection_state
{
    disconnected,
    connecting,
    connected,
    reconnecting,
    closing
};

typedef std::function<void(void)> con_listener;

typedef std::function<void(disconnect_reason reason)> close_listener;

typedef std::function<void(connection_error error)> fail_listener;

typedef std::function<void(connection_state)> state_listener;
```
#### Socket listeners
`void set_socket_open_listener(socket_listener const& l)`

Set listener for socket connect event, called when any sockets being ready to send message.

`void set_socket_close_listener(socket_listener const& l)`

Set listener for socket close event, called when any sockets being closed, afterward, corresponding `socket` object will be cleared from client.

`void clear_socket_listeners()`

**New in v3.2.0**: Clear both socket open and close listeners.

```C++
    //socket_listener declare:
    typedef std::function<void(std::string const& nsp)> socket_listener;
```

#### Connect and Close
`void connect(const std::string& uri)`

Connect to socket.io server. Protocol depends on build type:
- Debug build: `client.connect("http://localhost:3000");` (uses `ws://`)
- Release build: `client.connect("https://api.example.com:3000");` (uses `wss://`)

Additional connect overloads:
- `void connect(const std::string& uri, const message::ptr& auth)` - with authentication
- `void connect(const std::string& uri, const std::map<std::string, std::string>& query)` - with query params
- `void connect(const std::string& uri, const std::map<std::string, std::string>& query, const message::ptr& auth)`
- `void connect(const std::string& uri, const std::map<std::string, std::string>& query, const std::map<std::string, std::string>& http_extra_headers)`
- `void connect(const std::string& uri, const std::map<std::string, std::string>& query, const std::map<std::string, std::string>& http_extra_headers, const message::ptr& auth)`

`void close()`

Close the client, return immediately.

`void sync_close()`

Close the client, don't return until it is really closed.

`bool opened() const`

Check if client's connection is opened.

`connection_state get_connection_state() const`

**New in v3.2.0**: Get current connection state (disconnected, connecting, connected, reconnecting, closing).

#### Transparent reconnecting
`void set_reconnect_attempts(int attempts)`

Set max reconnect attempts, set to 0 to disable transparent reconnecting.

`void set_reconnect_delay(unsigned millis)`

Set minimum delay for reconnecting, this is the delay for 1st reconnecting attempt,
then the delay duration grows exponentially (base-2) by attempts made.

`void set_reconnect_delay_max(unsigned millis)`

Set maximum delay for reconnecting.

`void set_reconnect_config(const reconnect_config& config)`

**New in v3.2.0**: Configure all reconnection parameters at once using a `reconnect_config` struct.

```C++
// reconnect_config struct:
struct reconnect_config
{
    unsigned attempts = 0xFFFFFFFF;  // Infinite by default
    unsigned delay = 5000;           // 5 seconds
    unsigned delay_max = 25000;      // 25 seconds
    bool enabled = true;

    // Disable reconnection
    static reconnect_config disabled();
};
```

`void set_reconnecting_listener(con_listener const& l)`

Set listener for reconnecting is in process.

`void set_reconnect_listener(reconnect_listener const& l)`

Set listener for reconnecting event, called once a delayed connecting is scheduled.

#### Logs
`void set_logs_default()`

Configure logs to the default level (connect, disconnect, app)

`void set_logs_quiet()`

Configure logs to the quiet level

`void set_logs_verbose()`

Configure logs to the verbose level

#### Namespace
`socket::ptr socket(std::string const& nsp)`

Get a pointer to a socket which is paired with the specified namespace.

#### Session ID
`std::string const& get_sessionid() const`

Get socket.io session id.

#### Proxy Support
`void set_proxy_basic_auth(const std::string& uri, const std::string& username, const std::string& password)`

Configure HTTP proxy with basic authentication.

### *Message*
`message` Base class of all message object.

`int_message` message contains a 64-bit integer.

`double_message` message contains a double.

`string_message` message contains a string.

`array_message` message contains a `vector<message::ptr>`.

`object_message` message contains a `map<string,message::ptr>`.

`message::ptr` pointer to `message` object, it will be one of its derived classes, judge by `message.get_flag()`.

All designated constructor of `message` objects is hidden, you need to create message and get the `message::ptr` by `[derived]_message:create()`.

---

## New API Features (v3.2.0)

### Type-Safe Error Handling

**New in v3.2.0**: The library now provides detailed, type-safe error enums instead of generic callbacks.

#### Disconnect Reasons

When a connection closes, the `close_listener` receives a `disconnect_reason` enum that tells you exactly why:

```C++
h.set_close_listener([](sio::client::disconnect_reason reason) {
    switch(reason) {
        case sio::client::disconnect_reason::client_disconnect:
            std::cout << "User initiated disconnect" << std::endl;
            break;
        case sio::client::disconnect_reason::server_disconnect:
            std::cout << "Server closed the connection" << std::endl;
            break;
        case sio::client::disconnect_reason::transport_error:
            std::cout << "Network or WebSocket error" << std::endl;
            break;
        case sio::client::disconnect_reason::ping_timeout:
            std::cout << "Server stopped responding to pings" << std::endl;
            break;
        case sio::client::disconnect_reason::max_reconnect_attempts:
            std::cout << "Gave up after max reconnection attempts" << std::endl;
            break;
        case sio::client::disconnect_reason::transport_close:
            std::cout << "Transport closed cleanly" << std::endl;
            break;
        case sio::client::disconnect_reason::namespace_disconnect:
            std::cout << "Namespace-specific disconnect" << std::endl;
            break;
    }
});
```

#### Connection Errors

When a connection fails, the `fail_listener` receives a `connection_error` enum:

```C++
h.set_fail_listener([](sio::client::connection_error error) {
    switch(error) {
        case sio::client::connection_error::timeout:
            std::cout << "Connection timed out" << std::endl;
            break;
        case sio::client::connection_error::network_failure:
            std::cout << "Network unreachable or DNS failure" << std::endl;
            break;
        case sio::client::connection_error::protocol_error:
            std::cout << "Invalid socket.io protocol" << std::endl;
            break;
        case sio::client::connection_error::authentication_failed:
            std::cout << "Server rejected authentication" << std::endl;
            break;
        case sio::client::connection_error::transport_open_failed:
            std::cout << "WebSocket handshake failed" << std::endl;
            break;
        case sio::client::connection_error::ssl_error:
            std::cout << "TLS/SSL error" << std::endl;
            break;
        case sio::client::connection_error::unknown:
            std::cout << "Unknown error" << std::endl;
            break;
    }
});
```

#### Benefits

- **Type safety**: Can't accidentally compare error codes with integers
- **Better debugging**: Know exactly why connections fail
- **Actionable**: Different errors can trigger different recovery strategies
- **IDE support**: Autocomplete shows all possible error cases

### Reconnection Configuration

You can now configure all reconnection parameters at once using the `reconnect_config` struct:

```C++
sio::client h;

// Configure reconnection with custom parameters
sio::reconnect_config config(
    10,      // attempts: max reconnection attempts
    2000,    // delay: initial delay in milliseconds
    10000    // delay_max: maximum delay in milliseconds
);
h.set_reconnect_config(config);

// Or disable reconnection entirely
h.set_reconnect_config(sio::reconnect_config::disabled());

// Or use individual setters (legacy API still supported)
h.set_reconnect_attempts(10);
h.set_reconnect_delay(2000);
h.set_reconnect_delay_max(10000);
```

**Default values:**
- `attempts`: 0xFFFFFFFF (infinite)
- `delay`: 5000ms (5 seconds)
- `delay_max`: 25000ms (25 seconds)
- `enabled`: true

### Connection State Observer

Monitor connection state changes with the new state listener:

```C++
h.set_state_listener([](sio::client::connection_state state) {
    switch(state) {
        case sio::client::connection_state::disconnected:
            std::cout << "Disconnected" << std::endl;
            break;
        case sio::client::connection_state::connecting:
            std::cout << "Connecting..." << std::endl;
            break;
        case sio::client::connection_state::connected:
            std::cout << "Connected!" << std::endl;
            break;
        case sio::client::connection_state::reconnecting:
            std::cout << "Reconnecting..." << std::endl;
            break;
        case sio::client::connection_state::closing:
            std::cout << "Closing connection..." << std::endl;
            break;
    }
});

// Get current state
auto state = h.get_connection_state();
```

### Simplified Event Handlers with Auto-Ack

The new `on_with_ack()` method simplifies sending acknowledgments by providing the `ack_message` reference directly:

```C++
socket->on_with_ack("request", [](message::ptr const& msg, message::list& ack_msg) {
    // Process the request
    std::cout << "Received: " << msg->get_string() << std::endl;

    // Populate acknowledgment with custom data
    ack_msg.push(string_message::create("Success"));
    ack_msg.push(int_message::create(42));
    ack_msg.push(bool_message::create(true));
});
```

**Comparison with standard `on()` method:**

```C++
// Old way - check need_ack() manually
socket->on("request", [](sio::event& ev) {
    if (ev.need_ack()) {  // Manual check required
        message::list ack_msg;
        ack_msg.push(string_message::create("Success"));
        ev.put_ack_message(ack_msg);
    }
});

// New way - ack_msg provided automatically
socket->on_with_ack("request", [](message::ptr const& msg, message::list& ack_msg) {
    // No need to check - just populate ack_msg
    ack_msg.push(string_message::create("Success"));
});
```

**Benefits:**
- No need to check `need_ack()` - handled automatically
- Direct access to message without calling `ev.get_message()`
- Simpler function signature
- Acknowledgment only sent if you add data to `ack_msg`

**Complex acknowledgment example:**

```C++
socket->on_with_ack("getUser", [](message::ptr const& msg, message::list& ack_msg) {
    // Build complex response
    auto response = object_message::create();
    response->get_map()["status"] = string_message::create("success");
    response->get_map()["userId"] = int_message::create(123);
    response->get_map()["name"] = string_message::create("John Doe");

    ack_msg.push(response);
});
```

### Wildcard Event Listener

Listen to all events on a socket:

```C++
socket->on_any([](sio::event& ev) {
    std::cout << "Event: " << ev.get_name()
              << " on namespace: " << ev.get_nsp() << std::endl;
});

// Or with auxiliary format
socket->on_any([](const std::string& name, message::ptr const& msg,
                   bool need_ack, message::list& ack) {
    std::cout << "Any event: " << name << std::endl;
});
```

### Acknowledgment with Timeout

The timeout variant of `emit_with_ack()` allows you to handle cases where the server doesn't respond in time:

```C++
// Example: Request data with timeout
socket->emit_with_ack("fetchUser",
    message::list(int_message::create(userId)),
    [](message::list const& response) {
        // Success: server responded in time
        auto user = response.to_array_message()->get_vector()[0];
        std::cout << "User data: " << user->get_string() << std::endl;
    },
    3000,  // 3 second timeout
    []() {
        // Timeout: server didn't respond
        std::cerr << "Server response timeout!" << std::endl;
    });
```

**Key behaviors:**
- If the server responds before the timeout, the `ack` callback is invoked and the timeout timer is cancelled
- If the timeout expires before the server responds, the `timeout_callback` is invoked and the `ack` callback is cancelled
- Only one of the two callbacks will be invoked, never both
- The timeout is specified in milliseconds

**Use cases:**
- Network requests with latency requirements
- Real-time applications where stale data is useless
- Graceful degradation when server is slow or unresponsive
- Circuit breaker patterns

**Example with error handling:**

```C++
auto fetch_with_retry = [&](int userId, int retries) {
    socket->emit_with_ack("fetchUser",
        message::list(int_message::create(userId)),
        [userId](message::list const& response) {
            // Success
            std::cout << "Got user " << userId << std::endl;
        },
        2000,  // 2 second timeout
        [&, userId, retries]() {
            // Timeout - retry if attempts remain
            if (retries > 0) {
                std::cout << "Timeout, retrying... (" << retries << " left)" << std::endl;
                fetch_with_retry(userId, retries - 1);
            } else {
                std::cerr << "Failed to fetch user after all retries" << std::endl;
            }
        });
};

// Start with 3 retry attempts
fetch_with_retry(123, 3);
```

### Thread Safety

All socket operations are now thread-safe. You can safely add, remove, or trigger event listeners from multiple threads:

```C++
// Safe to call from multiple threads concurrently
std::thread t1([&]() {
    socket->on("event1", [](sio::event& ev) {
        // Handle event1
    });
});

std::thread t2([&]() {
    socket->on("event2", [](sio::event& ev) {
        // Handle event2
    });
});

std::thread t3([&]() {
    socket->off("event1");  // Safe even while event1 is being triggered
});
```

### Build-Type Protocol Selection

The library behavior changes based on how it's compiled:

#### Debug Build (Development)
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```
- Uses HTTP/WebSocket protocol (`ws://`)
- No TLS/SSL dependencies
- For local development: `client.connect("http://localhost:3000")`

#### Release Build (Production)
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```
- Uses HTTPS/WebSocket Secure protocol (`wss://`)
- Requires OpenSSL
- For production: `client.connect("https://api.example.com:3000")`

**Note**: The same library filename (`libsioclient.a`) is produced, but with different capabilities based on build type.

### Memory Management

All socket and client objects now use smart pointers internally:

```C++
// Automatic cleanup - no manual memory management needed
auto socket = client.socket("/chat");
socket->emit("message", "Hello");
// Socket will be cleaned up automatically
```

### Updated Dependencies

The library now uses modern, stable versions:

- **ASIO**: 1.36.0 (previously 1.10.2)
- **SimdJSON**: 3.10.1 (replaced RapidJSON for 2-3x faster JSON parsing)
- **WebSocketPP**: 0.8.2 (previously 0.7.0)
- **OpenSSL**: System version (Release builds only)

All dependencies are managed via git submodules for reproducible builds.

---

## Namespace Handler Helper (v3.2.0)

For convenience when working with a specific namespace, you can use the `namespace_handler` helper class:

```C++
#include "sio_namespace_handler.h"

sio::client client;
client.connect("https://api.example.com");

// Create a namespace handler for "/chat"
auto chat = sio::namespace_handler(client, "/chat");

// Use it just like a socket, but cached and more convenient
chat.on("message", [](sio::event& ev) {
    std::cout << "Message: " << ev.get_message()->get_string() << std::endl;
});

chat.emit("send_message", string_message::create("Hello!"));

// Check if namespace is valid
if (chat.is_valid()) {
    std::cout << "Namespace: " << chat.get_namespace() << std::endl;
}

// Get underlying socket for advanced operations
auto socket = chat.get_socket();
```

**Benefits:**
- Caches the socket pointer for the namespace
- Tracks registered events for easier cleanup
- Provides a cleaner API for single-namespace usage
- Thread-safe like the underlying socket

**Note:** This is a convenience wrapper - you can still use `client.socket("/chat")` directly.
