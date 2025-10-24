// C++20 Coroutine Example for Socket.IO C++ Client
// Demonstrates modern async/await style programming with socket.io

#include "sio_client.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>

using namespace sio;

std::mutex _lock;
std::condition_variable_any _cond;
bool connect_finish = false;
client h;

// Coroutine function that demonstrates sequential async operations
emit_task handle_user_flow(socket::ptr socket)
{
    std::cout << "\n=== Starting async user authentication flow ===" << std::endl;

    try
    {
        // Step 1: Authenticate user
        std::cout << "Step 1: Authenticating user..." << std::endl;
        message::list credentials;
        credentials.push(string_message::create("john_doe"));
        credentials.push(string_message::create("password123"));

        auto auth_response = co_await socket->emit_async("authenticate", credentials);
        std::cout << "Authentication successful! Received " << auth_response.size() << " response items" << std::endl;

        // Step 2: Get user data
        std::cout << "\nStep 2: Fetching user data..." << std::endl;
        auto user_data = co_await socket->emit_async("getUserData", auth_response);
        std::cout << "User data received: " << user_data.size() << " items" << std::endl;

        // Step 3: Get user profile
        std::cout << "\nStep 3: Loading user profile..." << std::endl;
        auto profile = co_await socket->emit_async("getProfile", user_data);
        std::cout << "Profile loaded successfully!" << std::endl;

        std::cout << "\n=== User flow completed successfully ===" << std::endl;
        co_return profile;
    }
    catch (const timeout_exception &e)
    {
        std::cerr << "ERROR: Operation timed out - " << e.what() << std::endl;
        co_return message::list();
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        co_return message::list();
    }
}

// Coroutine function demonstrating timeout handling
emit_task handle_with_timeout(socket::ptr socket)
{
    std::cout << "\n=== Demonstrating timeout handling ===" << std::endl;

    try
    {
        // This will timeout if server doesn't respond within 3 seconds
        std::cout << "Sending request with 3 second timeout..." << std::endl;
        auto response = co_await socket->emit_async("slowOperation", nullptr, 3000);
        std::cout << "Server responded in time!" << std::endl;
        co_return response;
    }
    catch (const timeout_exception &e)
    {
        std::cerr << "Request timed out: " << e.what() << std::endl;
        std::cerr << "Falling back to default behavior..." << std::endl;
        co_return message::list();
    }
}

// Coroutine function demonstrating multiple parallel operations
emit_task fetch_data(socket::ptr socket)
{
    std::cout << "\n=== Fetching multiple data sources ===" << std::endl;

    try
    {
        // Fetch user stats
        std::cout << "Fetching user stats..." << std::endl;
        auto stats = co_await socket->emit_async("getStats", nullptr);

        // Fetch notifications
        std::cout << "Fetching notifications..." << std::endl;
        auto notifications = co_await socket->emit_async("getNotifications", nullptr);

        // Fetch messages
        std::cout << "Fetching messages..." << std::endl;
        auto messages = co_await socket->emit_async("getMessages", nullptr);

        std::cout << "All data fetched successfully!" << std::endl;
        std::cout << "  - Stats: " << stats.size() << " items" << std::endl;
        std::cout << "  - Notifications: " << notifications.size() << " items" << std::endl;
        std::cout << "  - Messages: " << messages.size() << " items" << std::endl;

        co_return stats;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR fetching data: " << e.what() << std::endl;
        co_return message::list();
    }
}

void on_connected()
{
    _lock.lock();
    _cond.notify_all();
    connect_finish = true;
    _lock.unlock();
    std::cout << "Connected to server!" << std::endl;

    // Get the default namespace socket
    socket::ptr sock = h.socket();

    // Example 1: Simple async request
    std::cout << "\n--- Example 1: Simple Async Request ---" << std::endl;
    auto simple_task = sock->emit_async("simpleEvent", string_message::create("Hello from C++20!"));
    // Note: In a real application, you would co_await this in a coroutine context

    // Example 2: Chained operations (would be called from another coroutine)
    // handle_user_flow(sock);

    // Example 3: With timeout
    // handle_with_timeout(sock);

    // Example 4: Multiple sequential operations
    // fetch_data(sock);
}

void on_close(client::close_reason const &reason)
{
    std::cout << "Connection closed: " << reason << std::endl;
    _lock.lock();
    connect_finish = false;
    _lock.unlock();
}

void on_fail()
{
    std::cout << "Connection failed!" << std::endl;
    _lock.lock();
    connect_finish = false;
    _lock.unlock();
}

int main(int argc, char *argv[])
{
    std::cout << "Socket.IO C++20 Coroutine Example" << std::endl;
    std::cout << "==================================\n"
              << std::endl;

    // Set up connection listeners
    h.set_open_listener(&on_connected);
    h.set_close_listener(&on_close);
    h.set_fail_listener(&on_fail);

    // Connect to server
    std::string server_url = "http://localhost:3000";
    if (argc > 1)
    {
        server_url = argv[1];
    }

    std::cout << "Connecting to: " << server_url << std::endl;
    h.connect(server_url);

    // Wait for connection
    _lock.lock();
    if (!connect_finish)
    {
        _cond.wait(_lock);
    }
    _lock.unlock();

    if (connect_finish)
    {
        std::cout << "\n=== Connection established ===" << std::endl;
        std::cout << "Session ID: " << h.get_sessionid() << std::endl;

        // Keep the connection alive for demonstration
        std::cout << "\nPress Enter to disconnect and exit..." << std::endl;
        std::cin.get();

        h.sync_close();
        h.clear_con_listeners();
    }
    else
    {
        std::cout << "Failed to connect to server" << std::endl;
        return 1;
    }

    return 0;
}
